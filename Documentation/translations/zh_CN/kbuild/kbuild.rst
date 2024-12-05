.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/kbuild/kbuild.rst
:Translator: 慕冬亮 Dongliang Mu <dzm91@hust.edu.cn>

======
Kbuild
======


输出文件
========

modules.order
-------------
该文件记录模块在 Makefile 中出现的顺序。modprobe 使用该文件来确定性
解析匹配多个模块的别名。

modules.builtin
---------------
该文件列出了所有内置到内核中的模块。modprobe 使用该文件来避免尝试加载
内置模块时出错。

modules.builtin.modinfo
-----------------------
该文件包含所有内置模块的 modinfo。与单独模块的 modinfo 不同，所有字段
都带有模块名称前缀。

modules.builtin.ranges
----------------------
该文件包含所有内核内置模块的地址偏移范围（每个 ELF 节）。结合 System.map
文件，它可以用来将模块名称与符号关联起来。

环境变量
========

KCPPFLAGS
---------
在预处理时传递的额外选项。kbuild 进行所有预处理（包括构建 C 文件和汇编文件）
时，都会使用这些预处理选项。

KAFLAGS
-------
传递给汇编器的额外选项（适用于内置模块和外部模块）。

AFLAGS_MODULE
-------------
外部模块的额外汇编选项。

AFLAGS_KERNEL
-------------
内置模块的额外汇编选项。

KCFLAGS
-------
传递给 C 编译器的额外选项（适用于内置模块和外部模块）。

KRUSTFLAGS
----------
传递给 Rust 编译器的额外选项（适用于内置模块和外部模块）。

CFLAGS_KERNEL
-------------
在编译内置代码时，传递给 $(CC) 的额外选项。

CFLAGS_MODULE
-------------
编译外部模块时，传递给 $(CC) 的额外模块特定选项。

RUSTFLAGS_KERNEL
----------------
在编译内置代码时，传递给 $(RUSTC) 的额外选项。

RUSTFLAGS_MODULE
----------------
用于 $(RUSTC) 的额外模块特定选项。

LDFLAGS_MODULE
--------------
用于 $(LD) 链接模块时的额外选项。

HOSTCFLAGS
----------
在构建主机程序时传递给 $(HOSTCC) 的额外标志。

HOSTCXXFLAGS
------------
在构建主机程序时传递给 $(HOSTCXX) 的额外标志。

HOSTRUSTFLAGS
-------------
在构建主机程序时传递给 $(HOSTRUSTC) 的额外标志。

HOSTLDFLAGS
-----------
链接主机程序时传递的额外选项。

HOSTLDLIBS
----------
在构建主机程序时链接的额外库。

.. _zh_cn_userkbuildflags:

USERCFLAGS
----------
用于 $(CC) 编译用户程序（userprogs）时的额外选项。

USERLDFLAGS
-----------
用于 $(LD) 链接用户程序时的额外选项。用户程序（userprogs）是使用 CC 链接的，
因此 $(USERLDFLAGS) 应该根据需要包含 "-Wl," 前缀。

KBUILD_KCONFIG
--------------
将顶级 Kconfig 文件设置为此环境变量的值。默认名称为 "Kconfig"。

KBUILD_VERBOSE
--------------
设置 kbuild 的详细程度。可以分配与 "V=..." 相同的值。

有关完整列表，请参见 `make help`。

设置 "V=..." 优先于 KBUILD_VERBOSE。

KBUILD_EXTMOD
-------------
在构建外部模块时设置内核源代码的搜索目录。

设置 "M=..." 优先于 KBUILD_EXTMOD。

KBUILD_OUTPUT
-------------
指定内核构建的输出目录。

在单独的构建目录中为预构建内核构建外部模块时，这个变量也可以指向内核输出目录。请注意，
这并不指定外部模块本身的输出目录。

输出目录也可以使用 "O=..." 指定。

设置 "O=..." 优先于 KBUILD_OUTPUT。

KBUILD_EXTRA_WARN
-----------------
指定额外的构建检查。也可以通过在命令行传递 "W=..." 来设置相同的值。

请参阅 `make help` 了解支持的值列表。

设置 "W=..." 优先于 KBUILD_EXTRA_WARN。

KBUILD_DEBARCH
--------------
对于 deb-pkg 目标，允许覆盖 deb-pkg 部署的正常启发式方法。通常 deb-pkg 尝试根据
UTS_MACHINE 变量（在某些架构中还包括内核配置）来猜测正确的架构。KBUILD_DEBARCH
的值假定（不检查）为有效的 Debian 架构。

KDOCFLAGS
---------
指定在构建过程中用于 kernel-doc 检查的额外（警告/错误）标志，查看
scripts/kernel-doc 了解支持的标志。请注意，这目前不适用于文档构建。

ARCH
----
设置 ARCH 为要构建的架构。

在大多数情况下，架构的名称与 arch/ 目录中的子目录名称相同。

但某些架构（如 x86 和 sparc）有别名。

- x86: i386 表示 32 位，x86_64 表示 64 位
- parisc: parisc64 表示 64 位
- sparc: sparc32 表示 32 位，sparc64 表示 64 位

CROSS_COMPILE
-------------
指定 binutils 文件名的可选固定部分。CROSS_COMPILE 可以是文件名的一部分或完整路径。

在某些设置中，CROSS_COMPILE 也用于 ccache。

CF
--
用于 sparse 的额外选项。

CF 通常在命令行中如下所示使用::

    make CF=-Wbitwise C=2

INSTALL_PATH
------------
INSTALL_PATH 指定放置更新后的内核和系统映像的路径。默认值是 /boot，但你可以设置
为其他值。

INSTALLKERNEL
-------------
使用 "make install" 时调用的安装脚本。
默认名称是 "installkernel"。

该脚本将会以以下参数调用：

   - $1 - 内核版本
   - $2 - 内核映像文件
   - $3 - 内核映射文件
   - $4 - 默认安装路径（如果为空，则使用根目录）

"make install" 的实现是架构特定的，可能与上述有所不同。

提供 INSTALLKERNEL 以便在交叉编译内核时可以指定自定义安装程序。

MODLIB
------
指定模块的安装位置。
默认值为::

    $(INSTALL_MOD_PATH)/lib/modules/$(KERNELRELEASE)

该值可以被覆盖，在这种情况下将忽略默认值。

INSTALL_MOD_PATH
----------------
INSTALL_MOD_PATH 指定了模块目录重定位时 MODLIB 的前缀，通常由构建根
（build roots）所需。它没有在 makefile 中定义，但如果需要，可以作为
参数传递给 make。

INSTALL_MOD_STRIP
-----------------
如果 INSTALL_MOD_STRIP 被定义，内核模块在安装后会被剥离。如果
INSTALL_MOD_STRIP 的值为 '1'，则会使用默认选项 --strip-debug。否则，
INSTALL_MOD_STRIP 的值将作为 strip 命令的选项。

INSTALL_HDR_PATH
----------------
INSTALL_HDR_PATH 指定了执行 "make headers_*" 时，用户空间头文件的安装位置。

默认值为::

    $(objtree)/usr

$(objtree) 是保存输出文件的目录。
输出目录通常使用命令行中的 "O=..." 进行设置。

该值可以被覆盖，在这种情况下将忽略默认值。

INSTALL_DTBS_PATH
-----------------
INSTALL_DTBS_PATH 指定了设备树二进制文件的安装位置，通常由构建根（build roots）所需。
它没有在 makefile 中定义，但如果需要，可以作为参数传递给 make。

KBUILD_ABS_SRCTREE
--------------------------------------------------
Kbuild 在可能的情况下使用相对路径指向源代码树。例如，在源代码树中构建时，源代码树路径是
'.'。

设置该标志请求 Kbuild 使用源代码树的绝对路径。
在某些情况下这是有用的，例如在生成带有绝对路径条目的标签文件时等。

KBUILD_SIGN_PIN
---------------
当签署内核模块时，如果私钥需要密码或 PIN，此变量允许将密码或 PIN 传递给 sign-file 工具。

KBUILD_MODPOST_WARN
-------------------
KBUILD_MODPOST_WARN 可以设置为在最终模块链接阶段出现未定义符号时避免错误。它将这些错误
转为警告。

KBUILD_MODPOST_NOFINAL
----------------------
KBUILD_MODPOST_NOFINAL 可以设置为跳过模块的最终链接。这仅在加速编译测试时有用。

KBUILD_EXTRA_SYMBOLS
--------------------
用于依赖其他模块符号的模块。详见 modules.rst。

ALLSOURCE_ARCHS
---------------
对于 tags/TAGS/cscope 目标，可以指定包含在数据库中的多个架构，用空格分隔。例如::

    $ make ALLSOURCE_ARCHS="x86 mips arm" tags

要获取所有可用架构，也可以指定 all。例如::

    $ make ALLSOURCE_ARCHS=all tags

IGNORE_DIRS
-----------
对于 tags/TAGS/cscope 目标，可以选择不包含在数据库中的目录，用空格分隔。例如::

    $ make IGNORE_DIRS="drivers/gpu/drm/radeon tools" cscope

KBUILD_BUILD_TIMESTAMP
----------------------
将该环境变量设置为日期字符串，可以覆盖在 UTS_VERSION 定义中使用的时间戳
（运行内核时的 uname -v）。该值必须是一个可以传递给 date -d 的字符串。默认值是
内核构建某个时刻的 date 命令输出。

KBUILD_BUILD_USER, KBUILD_BUILD_HOST
------------------------------------
这两个变量允许覆盖启动时显示的 user@host 字符串以及 /proc/version 中的信息。
默认值分别是 whoami 和 host 命令的输出。

LLVM
----
如果该变量设置为 1，Kbuild 将使用 Clang 和 LLVM 工具，而不是 GCC 和 GNU
binutils 来构建内核。

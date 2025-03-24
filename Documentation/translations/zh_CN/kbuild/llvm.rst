.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/kbuild/llvm.rst
:Translator: 慕冬亮 Dongliang Mu <dzm91@hust.edu.cn>

==========================
使用 Clang/LLVM 构建 Linux
==========================

本文档介绍如何使用 Clang 和 LLVM 工具构建 Linux 内核。

关于
----

Linux 内核传统上一直使用 GNU 工具链（如 GCC 和 binutils）进行编译。持续的工作使得
`Clang <https://clang.llvm.org/>`_ 和 `LLVM <https://llvm.org/>`_ 工具可
作为可行的替代品。一些发行版，如 `Android <https://www.android.com/>`_、
`ChromeOS <https://www.chromium.org/chromium-os>`_、`OpenMandriva
<https://www.openmandriva.org/>`_ 和 `Chimera Linux
<https://chimera-linux.org/>`_ 使用 Clang 编译的内核。谷歌和 Meta 的数据中心
集群也运行由 Clang 编译的内核。

`LLVM 是由 C++ 对象实现的工具链组件集合 <https://www.aosabook.org/en/llvm.html>`_。
Clang 是 LLVM 的前端，支持 C 语言和内核所需的 GNU C 扩展，其发音为 "klang"，而非
"see-lang"。

使用 LLVM 构建
--------------

通过以下命令调用 ``make``::

	make LLVM=1

为主机目标进行编译。对于交叉编译::

	make LLVM=1 ARCH=arm64

LLVM= 参数
----------

LLVM 有 GNU binutils 工具的替代品。这些工具可以单独启用。以下是支持的 make 变量
完整列表::

	make CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm STRIP=llvm-strip \
	  OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump READELF=llvm-readelf \
	  HOSTCC=clang HOSTCXX=clang++ HOSTAR=llvm-ar HOSTLD=ld.lld

``LLVM=1`` 扩展为上述命令。

如果你的 LLVM 工具不在 PATH 中，你可以使用以斜杠结尾的 LLVM 变量提供它们的位置::

	make LLVM=/path/to/llvm/

这将使用 ``/path/to/llvm/clang``、``/path/to/llvm/ld.lld`` 等工具。也可以
使用以下命令::

	PATH=/path/to/llvm:$PATH make LLVM=1

如果你的 LLVM 工具带有版本后缀，并且你希望测试该特定版本而非无后缀的可执行文件，
类似于 ``LLVM=1``，你可以使用 ``LLVM`` 变量传递该后缀::

	make LLVM=-14

这将使用 ``clang-14``、``ld.lld-14`` 等工具。为了支持带有版本后缀的树外路径组合，
我们建议::

	PATH=/path/to/llvm/:$PATH make LLVM=-14

``LLVM=0`` 与省略 ``LLVM`` 完全不同，它将表现得像 ``LLVM=1``。如果你只希望使用
某些 LLVM 工具，请使用它们各自的 make 变量。

在通过不同命令配置和构建时，应为每次调用 ``make`` 设置相同的 ``LLVM=`` 值。如果
运行的脚本最终会调用 ``make``，则还应将 ``LLVM=`` 设置为环境变量。

交叉编译
--------

单个 Clang 编译器二进制文件（及其对应的 LLVM 工具）通常会包含所有支持的后端，这可以
简化交叉编译，尤其是使用 ``LLVM=1`` 时。如果仅使用 LLVM 工具，``CROSS_COMPILE``
或目标三元组前缀就变得不必要。示例::

	make LLVM=1 ARCH=arm64

作为混合 LLVM 和 GNU 工具的示例，对于像 ``ARCH=s390`` 这样目前尚不支持
``ld.lld`` 或 ``llvm-objcopy`` 的目标，你可以通过以下方式调用 ``make``::

	make LLVM=1 ARCH=s390 LD=s390x-linux-gnu-ld.bfd \
	  OBJCOPY=s390x-linux-gnu-objcopy

此示例将调用 ``s390x-linux-gnu-ld.bfd`` 作为链接器和
``s390x-linux-gnu-objcopy``，因此请确保它们在你的 ``$PATH`` 中。

当 ``LLVM=1`` 未设置时，``CROSS_COMPILE`` 不会用于给 Clang 编译器二进制文件
（或相应的 LLVM 工具）添加前缀，而 GNU 工具则需要这样做。

LLVM_IAS= 参数
--------------

Clang 可以编译汇编代码。你可以传递 ``LLVM_IAS=0`` 禁用此行为，使 Clang 调用
相应的非集成汇编器。示例::

	make LLVM=1 LLVM_IAS=0

在交叉编译时，你需要使用 ``CROSS_COMPILE`` 与 ``LLVM_IAS=0``，从而设置
``--prefix=`` 使得编译器可以对应的非集成汇编器（通常，在面向另一种架构时，
你不想使用系统汇编器）。例如::

	make LLVM=1 ARCH=arm LLVM_IAS=0 CROSS_COMPILE=arm-linux-gnueabi-

Ccache
------

``ccache`` 可以与 ``clang`` 一起使用，以改善后续构建（尽管在不同构建之间
KBUILD_BUILD_TIMESTAMP_ 应设置为同一确定值，以避免 100% 的缓存未命中，
详见 Reproducible_builds_ 获取更多信息）::

	KBUILD_BUILD_TIMESTAMP='' make LLVM=1 CC="ccache clang"

.. _KBUILD_BUILD_TIMESTAMP: kbuild.html#kbuild-build-timestamp
.. _Reproducible_builds: reproducible-builds.html#timestamps

支持的架构
----------

LLVM 并不支持 Linux 内核所有可支持的架构，同样，即使 LLVM 支持某一架构，也并不意味着在
该架构下内核可以正常构建或工作。以下是当前 ``CC=clang`` 或 ``LLVM=1`` 支持的架构总结。
支持级别对应于 MAINTAINERS 文件中的 "S" 值。如果某个架构未列出，则表示 LLVM 不支持它
或存在已知问题。使用最新的稳定版 LLVM 或甚至开发版本通常会得到最佳结果。一个架构的
``defconfig`` 通常预期能够良好工作，但某些配置可能存在尚未发现的问题。欢迎在以下
问题跟踪器中提交错误报告！

.. list-table::
   :widths: 10 10 10
   :header-rows: 1

   * - 架构
     - 支持级别
     - ``make`` 命令
   * - arm
     - 支持
     - ``LLVM=1``
   * - arm64
     - 支持
     - ``LLVM=1``
   * - hexagon
     - 维护
     - ``LLVM=1``
   * - loongarch
     - 维护
     - ``LLVM=1``
   * - mips
     - 维护
     - ``LLVM=1``
   * - powerpc
     - 维护
     - ``LLVM=1``
   * - riscv
     - 支持
     - ``LLVM=1``
   * - s390
     - 维护
     - ``LLVM=1`` （LLVM >= 18.1.0），``CC=clang`` （LLVM < 18.1.0）
   * - um (用户模式)
     - 维护
     - ``LLVM=1``
   * - x86
     - 支持
     - ``LLVM=1``

获取帮助
--------

- `网站 <https://clangbuiltlinux.github.io/>`_
- `邮件列表 <https://lore.kernel.org/llvm/>`_: <llvm@lists.linux.dev>
- `旧邮件列表档案 <https://groups.google.com/g/clang-built-linux>`_
- `问题跟踪器 <https://github.com/ClangBuiltLinux/linux/issues>`_
- IRC: #clangbuiltlinux 在 irc.libera.chat
- `Telegram <https://t.me/ClangBuiltLinux>`_: @ClangBuiltLinux
- `维基 <https://github.com/ClangBuiltLinux/linux/wiki>`_
- `初学者问题 <https://github.com/ClangBuiltLinux/linux/issues?q=is%3Aopen+is%3Aissue+label%3A%22good+first+issue%22>`_

.. _zh_cn_getting_llvm:

获取 LLVM
---------

我们在 `kernel.org <https://kernel.org/pub/tools/llvm/>`_ 提供预编译的稳定版 LLVM。
这些版本已经针对 Linux 内核构建，使用配置文件数据进行优化。相较于其他发行版中的 LLVM，它们应该
能提高内核构建效率。

以下是一些有助于从源代码构建 LLVM 或通过发行版的包管理器获取 LLVM 的链接。

- https://releases.llvm.org/download.html
- https://github.com/llvm/llvm-project
- https://llvm.org/docs/GettingStarted.html
- https://llvm.org/docs/CMake.html
- https://apt.llvm.org/
- https://www.archlinux.org/packages/extra/x86_64/llvm/
- https://github.com/ClangBuiltLinux/tc-build
- https://github.com/ClangBuiltLinux/linux/wiki/Building-Clang-from-source
- https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/

.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/rust/quick-start.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>


快速入门
========

本文介绍了如何开始使用Rust进行内核开发。


构建依赖
--------

本节描述了如何获取构建所需的工具。

其中一些依赖也许可以从Linux发行版中获得，包名可能是 ``rustc`` , ``rust-src`` ,
``rust-bindgen`` 等。然而，在写这篇文章的时候，它们很可能还不够新，除非发行版跟踪最
新的版本。

为了方便检查是否满足要求，可以使用以下目标::

	make LLVM=1 rustavailable

这会触发与Kconfig用来确定是否应该启用 ``RUST_IS_AVAILABLE`` 相同的逻辑；不过，如
果Kconfig认为不该启用，它会列出未满足的条件。


rustc
*****

需要一个特定版本的Rust编译器。较新的版本可能会也可能不会工作，因为就目前而言，内核依赖
于一些不稳定的Rust特性。

如果使用的是 ``rustup`` ，请进入内核编译目录（或者用 ``--path=<build-dir>`` 参数
来 ``设置`` sub-command)并运行::

	rustup override set $(scripts/min-tool-version.sh rustc)

+这将配置你的工作目录使用正确版本的 ``rustc``，而不影响你的默认工具链。

请注意覆盖应用当前的工作目录（和它的子目录）。

如果你使用 ``rustup``， 可以从下面的链接拉取一个单独的安装程序:

	https://forge.rust-lang.org/infra/other-installation-methods.html#standalone


Rust标准库源代码
****************

Rust标准库的源代码是必需的，因为构建系统会交叉编译 ``core`` 和 ``alloc`` 。

如果正在使用 ``rustup`` ，请运行::

	rustup component add rust-src

这些组件是按工具链安装的，因此以后升级Rust编译器版本需要重新添加组件。

否则，如果使用独立的安装程序，可以将Rust源码树下载到安装工具链的文件夹中::

       curl -L "https://static.rust-lang.org/dist/rust-src-$(scripts/min-tool-version.sh rustc).tar.gz" |
               tar -xzf - -C "$(rustc --print sysroot)/lib" \
               "rust-src-$(scripts/min-tool-version.sh rustc)/rust-src/lib/" \
               --strip-components=3

在这种情况下，以后升级Rust编译器版本需要手动更新这个源代码树（这可以通过移除
``$(rustc --print sysroot)/lib/rustlib/src/rust`` ，然后重新执行上
面的命令做到）。


libclang
********

``bindgen`` 使用 ``libclang`` （LLVM的一部分）来理解内核中的C代码，这意味着需要安
装LLVM；同在开启``LLVM=1`` 时编译内核一样。

Linux发行版中可能会有合适的包，所以最好先检查一下。

适用于部分系统和架构的二进制文件也可到以下网址下载：

	https://releases.llvm.org/download.html

或者自行构建LLVM，这需要相当长的时间，但并不是一个复杂的过程：

	https://llvm.org/docs/GettingStarted.html#getting-the-source-code-and-building-llvm

请参阅 Documentation/kbuild/llvm.rst 了解更多信息，以及获取预构建版本和发行包
的进一步方法。


bindgen
*******

内核的C端绑定是在构建时使用 ``bindgen`` 工具生成的。这需要特定的版本。

通过以下方式安装它（注意，这将从源码下载并构建该工具）::

	cargo install --locked --version $(scripts/min-tool-version.sh bindgen) bindgen-cli

``bindgen`` 需要找到合适的 ``libclang`` 才能工作。如果没有找到（或者找到的
``libclang`` 与应该使用的 ``libclang`` 不同），则可以使用 ``clang-sys``
理解的环境变量（Rust绑定创建的 ``bindgen`` 用来访问 ``libclang``）:


* ``LLVM_CONFIG_PATH`` 可以指向一个 ``llvm-config`` 可执行文件。

* 或者 ``LIBCLANG_PATH`` 可以指向 ``libclang`` 共享库或包含它的目录。

* 或者 ``CLANG_PATH`` 可以指向 ``clang`` 可执行文件。

详情请参阅 ``clang-sys`` 的文档:


开发依赖
--------

本节解释了如何获取开发所需的工具。也就是说，在构建内核时不需要这些工具。


rustfmt
*******

``rustfmt`` 工具被用来自动格式化所有的Rust内核代码，包括生成的C绑定（详情请见
coding-guidelines.rst ）。

如果使用的是 ``rustup`` ，它的 ``默认`` 配置文件已经安装了这个工具，因此不需要做什么。
如果使用的是其他配置文件，可以手动安装该组件::

	rustup component add rustfmt

独立的安装程序也带有 ``rustfmt`` 。


clippy
******

``clippy`` 是一个Rust linter。运行它可以为Rust代码提供额外的警告。它可以通过向 ``make``
传递 ``CLIPPY=1`` 来运行（关于细节，详见 general-information.rst ）。

如果正在使用 ``rustup`` ，它的 ``默认`` 配置文件已经安装了这个工具，因此不需要做什么。
如果使用的是另一个配置文件，该组件可以被手动安装::

	rustup component add clippy

独立的安装程序也带有 ``clippy`` 。


cargo
*****

``cargo`` 是Rust的本地构建系统。目前需要它来运行测试，因为它被用来构建一个自定义的标准
库，其中包含了内核中自定义 ``alloc`` 所提供的设施。测试可以使用 ``rusttest`` Make 目标
来运行。

如果使用的是 ``rustup`` ，所有的配置文件都已经安装了该工具，因此不需要再做什么。

独立的安装程序也带有 ``cargo`` 。


rustdoc
*******

``rustdoc`` 是Rust的文档工具。它为Rust代码生成漂亮的HTML文档（详情请见 general-information.rst ）。

``rustdoc`` 也被用来测试文档化的Rust代码中提供的例子（称为doctests或文档测试）。
``rusttest`` 是本功能的Make目标。

如果使用的是 ``rustup`` ，所有的配置文件都已经安装了这个工具，因此不需要做什么。

独立的安装程序也带有 ``rustdoc`` 。


rust-analyzer
*************

`rust-analyzer <https://rust-analyzer.github.io/>`_ 语言服务器可以和许多编辑器
一起使用，以实现语法高亮、补全、转到定义和其他功能。

``rust-analyzer`` 需要一个配置文件， ``rust-project.json``, 它可以由 ``rust-analyzer``
Make 目标生成::

       make LLVM=1 rust-analyzer


配置
----

Rust支持（CONFIG_RUST）需要在 ``General setup`` 菜单中启用。在其他要求得到满足的情
况下，该选项只有在找到合适的Rust工具链时才会显示（见上文）。相应的，这将使依赖Rust的其
他选项可见。

之后，进入::

	Kernel hacking
	    -> Sample kernel code
	        -> Rust samples

并启用一些内置或可加载的样例模块。


构建
----

用完整的LLVM工具链构建内核是目前支持的最佳设置。即::

	make LLVM=1

使用GCC对某些配置也是可行的，但目前它是非常试验性的。


折腾
----

要想深入了解，请看 ``samples/rust/`` 下的样例源代码、 ``rust/`` 下的Rust支持代码和
``Kernel hacking`` 下的 ``Rust hacking`` 菜单。

如果使用的是GDB/Binutils，而Rust符号没有被demangled，原因是工具链还不支持Rust的新v0
mangling方案。有几个办法可以解决：

  - 安装一个较新的版本（GDB >= 10.2, Binutils >= 2.36）。

  - 一些版本的GDB（例如vanilla GDB 10.1）能够使用嵌入在调试信息(``CONFIG_DEBUG_INFO``)
    中的pre-demangled的名字。

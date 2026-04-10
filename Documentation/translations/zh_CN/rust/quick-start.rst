.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/rust/quick-start.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>


快速入门
========

本文介绍了如何开始使用Rust进行内核开发。

安装内核开发所需的 Rust 工具链有几种方式。一种简单的方式是使用 Linux 发行版的软件包
（如果它们合适的话）——下面的第一节解释了这种方法。这种方法的一个优势是，通常发行版会
匹配 Rust 和 Clang 所使用的 LLVM。

另一种方式是使用 `kernel.org <https://kernel.org/pub/tools/llvm/rust/>`_ 上提
供的预构建稳定版本的 LLVM+Rust。这些与 :ref:`获取 LLVM <zh_cn_getting_llvm>` 中的精
简快速 LLVM 工具链相同，并添加了 Rust for Linux 支持的 Rust 版本。提供了两套工具
链："最新 LLVM" 和 "匹配 LLVM"（请参阅链接了解更多信息）。

或者，接下来的两个 "依赖" 章节将解释每个组件以及如何通过 ``rustup``、Rust 的独立
安装程序或从源码构建来安装它们。

本文档的其余部分解释了有关如何入门的其他方面。


发行版
------

Arch Linux
**********

Arch Linux 提供较新的 Rust 版本，因此通常开箱即用，例如::

	pacman -S rust rust-src rust-bindgen


Debian
******

Debian 13（Trixie）以及 Testing 和 Debian Unstable（Sid）提供较新的 Rust 版
本，因此通常开箱即用，例如::

	apt install rustc rust-src bindgen rustfmt rust-clippy


Fedora Linux
************

Fedora Linux 提供较新的 Rust 版本，因此通常开箱即用，例如::

	dnf install rust rust-src bindgen-cli rustfmt clippy


Gentoo Linux
************

Gentoo Linux（尤其是 testing 分支）提供较新的 Rust 版本，因此通常开箱即用，
例如::

	USE='rust-src rustfmt clippy' emerge dev-lang/rust dev-util/bindgen

可能需要设置 ``LIBCLANG_PATH``。


Nix
***

Nix（unstable 频道）提供较新的 Rust 版本，因此通常开箱即用，例如::

	{ pkgs ? import <nixpkgs> {} }:
	pkgs.mkShell {
	  nativeBuildInputs = with pkgs; [ rustc rust-bindgen rustfmt clippy ];
	  RUST_LIB_SRC = "${pkgs.rust.packages.stable.rustPlatform.rustLibSrc}";
	}


openSUSE
********

openSUSE Slowroll 和 openSUSE Tumbleweed 提供较新的 Rust 版本，因此通常开箱
即用，例如::

	zypper install rust rust1.79-src rust-bindgen clang


Ubuntu
******

25.04
~~~~~

最新的 Ubuntu 版本提供较新的 Rust 版本，因此通常开箱即用，例如::

	apt install rustc rust-src bindgen rustfmt rust-clippy

此外，需要设置 ``RUST_LIB_SRC``，例如::

	RUST_LIB_SRC=/usr/src/rustc-$(rustc --version | cut -d' ' -f2)/library

为方便起见，可以将 ``RUST_LIB_SRC`` 导出到全局环境中。


24.04 LTS 及更早版本
~~~~~~~~~~~~~~~~~~~~

虽然 Ubuntu 24.04 LTS 及更早版本仍然提供较新的 Rust 版本，但它们需要一些额外的配
置，使用带版本号的软件包，例如::

	apt install rustc-1.80 rust-1.80-src bindgen-0.65 rustfmt-1.80 \
		rust-1.80-clippy
	ln -s /usr/lib/rust-1.80/bin/rustfmt /usr/bin/rustfmt-1.80
	ln -s /usr/lib/rust-1.80/bin/clippy-driver /usr/bin/clippy-driver-1.80

这些软件包都不会将其工具设置为默认值；因此应该显式指定它们，例如::

	make LLVM=1 RUSTC=rustc-1.80 RUSTDOC=rustdoc-1.80 RUSTFMT=rustfmt-1.80 \
		CLIPPY_DRIVER=clippy-driver-1.80 BINDGEN=bindgen-0.65

或者，修改 ``PATH`` 变量将 Rust 1.80 的二进制文件放在前面，并将 ``bindgen`` 设
置为默认值，例如::

	PATH=/usr/lib/rust-1.80/bin:$PATH
	update-alternatives --install /usr/bin/bindgen bindgen \
		/usr/bin/bindgen-0.65 100
	update-alternatives --set bindgen /usr/bin/bindgen-0.65

使用带版本号的软件包时需要设置 ``RUST_LIB_SRC``，例如::

	RUST_LIB_SRC=/usr/src/rustc-$(rustc-1.80 --version | cut -d' ' -f2)/library

为方便起见，可以将 ``RUST_LIB_SRC`` 导出到全局环境中。

此外， ``bindgen-0.65`` 在较新的版本（24.04 LTS 和 24.10）中可用，但在更早的版
本（20.04 LTS 和 22.04 LTS）中可能不可用，因此可能需要手动构建 ``bindgen``
（请参见下文）。


构建依赖
--------

本节描述了如何获取构建所需的工具。

为了方便检查是否满足要求，可以使用以下目标::

	make LLVM=1 rustavailable

这会触发与Kconfig用来确定是否应该启用 ``RUST_IS_AVAILABLE`` 相同的逻辑；不过，如
果Kconfig认为不该启用，它会列出未满足的条件。


rustc
*****

需要一个较新版本的Rust编译器。

如果使用的是 ``rustup`` ，请进入内核编译目录（或者用 ``--path=<build-dir>`` 参数
来 ``设置`` sub-command)，例如运行::

	rustup override set stable

这将配置你的工作目录使用给定版本的 ``rustc``，而不影响你的默认工具链。

请注意覆盖应用当前的工作目录（和它的子目录）。

如果你使用 ``rustup``， 可以从下面的链接拉取一个单独的安装程序:

	https://forge.rust-lang.org/infra/other-installation-methods.html#standalone


Rust标准库源代码
****************

Rust标准库的源代码是必需的，因为构建系统会交叉编译 ``core`` 。

如果正在使用 ``rustup`` ，请运行::

	rustup component add rust-src

这些组件是按工具链安装的，因此以后升级Rust编译器版本需要重新添加组件。

否则，如果使用独立的安装程序，可以将Rust源码树下载到安装工具链的文件夹中::

	curl -L "https://static.rust-lang.org/dist/rust-src-$(rustc --version | cut -d' ' -f2).tar.gz" |
		tar -xzf - -C "$(rustc --print sysroot)/lib" \
		"rust-src-$(rustc --version | cut -d' ' -f2)/rust-src/lib/" \
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

内核的C端绑定是在构建时使用 ``bindgen`` 工具生成的。

例如，通过以下方式安装它（注意，这将从源码下载并构建该工具）::

	cargo install --locked bindgen-cli

``bindgen`` 使用 ``clang-sys`` crate 来查找合适的 ``libclang`` （可以静态链
接、动态链接或在运行时加载）。默认情况下，上面的 ``cargo`` 命令会生成一个在运行时
加载 ``libclang`` 的 ``bindgen`` 二进制文件。如果没有找到（或者应该使用与找到的
不同的 ``libclang``），可以调整该过程，例如使用 ``LIBCLANG_PATH`` 环境变量。详
情请参阅 ``clang-sys`` 的文档：

	https://github.com/KyleMayes/clang-sys#linking

	https://github.com/KyleMayes/clang-sys#environment-variables


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

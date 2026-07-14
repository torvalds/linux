.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/rust/general-information.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>


基本信息
========

本文档包含了在内核中使用Rust支持时需要了解的有用信息。

``no_std``
----------

内核中的 Rust 支持只能链接 `core <https://doc.rust-lang.org/core/>`_，
而不能链接 `std <https://doc.rust-lang.org/std/>`_。供内核使用的 crate
必须使用 ``#![no_std]`` 属性选择这种行为。


.. _rust_code_documentation_zh_cn:

代码文档
--------

Rust内核代码使用其内置的文档生成器 ``rustdoc`` 进行记录。

生成的 HTML 文档包括集成搜索、链接项（如类型、函数、常量）、源代码等。
它们可以在以下地址阅读：

		https://rust.docs.kernel.org

对于 linux-next，请参阅：

		https://rust.docs.kernel.org/next/

每个主要版本也有对应的标签，例如：

		https://rust.docs.kernel.org/6.10/

这些文档也可以很容易地在本地生成和阅读。这相当快（与编译代码本身的顺序相同），而且不需要特
殊的工具或环境。这有一个额外的好处，那就是它们将根据所使用的特定内核配置进行定制。要生成它
们，请使用 ``rustdoc`` 目标，并使用编译时使用的相同调用，例如::

	make LLVM=1 rustdoc

要在你的网络浏览器中本地阅读该文档，请运行如::

	xdg-open Documentation/output/rust/rustdoc/kernel/index.html

要了解如何编写文档，请看 coding-guidelines.rst 。


额外的lints
-----------

虽然 ``rustc`` 是一个非常有用的编译器，但一些额外的lints和分析可以通过 ``clippy``
（一个Rust linter）来实现。要启用它，请将CLIPPY=1传递到用于编译的同一调用中，例如::

	make LLVM=1 CLIPPY=1

请注意，Clippy可能会改变代码生成，因此在构建产品内核时不应该启用它。

抽象和绑定
----------

抽象是用Rust代码包装来自C端的内核功能。

为了使用来自C端的函数和类型，需要创建绑定。绑定是Rust对那些来自C端的函数和类型的声明。

例如，人们可以在Rust中写一个 ``Mutex`` 抽象，它从C端包装一个 ``Mutex结构体`` ，并
通过绑定调用其函数。

抽象并不能用于所有的内核内部API和概念，但随着时间的推移，我们打算扩大覆盖范围。“Leaf”
模块（例如，驱动程序）不应该直接使用C语言的绑定。相反，子系统应该根据需要提供尽可能安
全的抽象。

.. code-block::

	                                                rust/bindings/
	                                               (rust/helpers/)

	                                                   include/ -----+ <-+
	                                                                 |   |
	  drivers/              rust/kernel/              +----------+ <-+   |
	    fs/                                           | bindgen  |       |
	   .../            +-------------------+          +----------+ --+   |
	                   |    Abstractions   |                         |   |
	+---------+        | +------+ +------+ |          +----------+   |   |
	| my_foo  | -----> | | foo  | | bar  | | -------> | Bindings | <-+   |
	| driver  |  Safe  | | sub- | | sub- | |  Unsafe  |          |       |
	+---------+        | |system| |system| |          | bindings | <-----+
	     |             | +------+ +------+ |          |  crate   |       |
	     |             |   kernel crate    |          +----------+       |
	     |             +-------------------+                             |
	     |                                                               |
	     +------------------# FORBIDDEN #--------------------------------+

主要思想是将所有与内核 C API 的直接交互封装到经过仔细审查和文档化的抽象
中。这样，只要满足以下条件，这些抽象的用户就不能引入未定义行为
（undefined behavior，UB）：

#. 抽象是正确的（"可靠"）。
#. 任何 ``unsafe`` 块都遵守调用块内操作所需的安全契约。类似地，任何
   ``unsafe impl`` 都遵守实现该特性所需的安全契约。

绑定
~~~~

通过从 ``include/`` 中将 C 头文件包含到
``rust/bindings/bindings_helper.h``， ``bindgen`` 工具将为所包含的子系统
自动生成绑定。构建后，请查看 ``rust/bindings/`` 目录中的
``*_generated.rs`` 输出文件。

对于 ``bindgen`` 不会自动生成的 C 头文件部分，例如 C ``inline`` 函数或
非平凡宏，可以在 ``rust/helpers/`` 中添加一个小型包装函数，使其也可供
Rust 端使用。

抽象
~~~~

抽象是绑定和内核内用户之间的层。它们位于 ``rust/kernel/`` 中，其作用是
将对绑定的不安全访问封装到尽可能安全并暴露给用户的 API 中。抽象的用户
包括用 Rust 编写的驱动程序或文件系统等。

除了安全方面，这些抽象还应该易于使用，也就是说，把 C 接口转换为符合
Rust 惯例的代码。基本示例包括将 C 的资源获取和释放转换为 Rust 的初始化
和清理模式，或者将 C 整数错误码转换为 Rust 的 ``Result``。


有条件的编译
------------

Rust代码可以访问基于内核配置的条件性编译:

.. code-block:: rust

	#[cfg(CONFIG_X)]       // Enabled               (`y` or `m`)
	#[cfg(CONFIG_X="y")]   // Enabled as a built-in (`y`)
	#[cfg(CONFIG_X="m")]   // Enabled as a module   (`m`)
	#[cfg(not(CONFIG_X))]  // Disabled

对于 Rust 的 ``cfg`` 不支持的其他条件，例如带有数值比较的表达式，可以
定义一个新的 Kconfig 符号：

.. code-block:: kconfig

	config RUSTC_HAS_SPAN_FILE
		def_bool RUSTC_VERSION >= 108800

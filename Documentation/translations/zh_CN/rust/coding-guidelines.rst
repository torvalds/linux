.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/rust/coding-guidelines.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

编码指南
========

本文档描述了如何在内核中编写Rust代码。


风格和格式化
------------

代码应该使用 ``rustfmt`` 进行格式化。这样一来，一个不时为内核做贡献的人就不需要再去学
习和记忆一个样式指南了。更重要的是，审阅者和维护者不需要再花时间指出风格问题，这样就可以
减少补丁落地所需的邮件往返。

.. note::  ``rustfmt`` 不检查注释和文档的约定。因此，这些仍然需要照顾到。

使用 ``rustfmt`` 的默认设置。这意味着遵循Rust的习惯性风格。例如，缩进时使用4个空格而
不是制表符。

在输入、保存或提交时告知编辑器/IDE进行格式化是很方便的。然而，如果因为某些原因需要在某
个时候重新格式化整个内核Rust的源代码，可以运行以下程序::

	make LLVM=1 rustfmt

也可以检查所有的东西是否都是格式化的（否则就打印一个差异），例如对于一个CI，用::

	make LLVM=1 rustfmtcheck

像内核其他部分的 ``clang-format`` 一样， ``rustfmt`` 在单个文件上工作，并且不需要
内核配置。有时，它甚至可以与破碎的代码一起工作。


注释
----

“普通”注释（即以 ``//`` 开头，而不是 ``///`` 或 ``//!`` 开头的代码文档）的写法与文
档注释相同，使用Markdown语法，尽管它们不会被渲染。这提高了一致性，简化了规则，并允许在
这两种注释之间更容易地移动内容。比如说:

.. code-block:: rust

	// `object` is ready to be handled now.
	f(object);

此外，就像文档一样，注释在句子的开头要大写，并以句号结束（即使是单句）。这包括 ``// SAFETY:``,
``// TODO:`` 和其他“标记”的注释，例如:

.. code-block:: rust

	// FIXME: The error should be handled properly.

注释不应该被用于文档的目的：注释是为了实现细节，而不是为了用户。即使源文件的读者既是API
的实现者又是用户，这种区分也是有用的。事实上，有时同时使用注释和文档是很有用的。例如，用
于 ``TODO`` 列表或对文档本身的注释。对于后一种情况，注释可以插在中间；也就是说，离要注
释的文档行更近。对于其他情况，注释会写在文档之后，例如:

.. code-block:: rust

	/// Returns a new [`Foo`].
	///
	/// # Examples
	///
	// TODO: Find a better example.
	/// ```
	/// let foo = f(42);
	/// ```
	// FIXME: Use fallible approach.
	pub fn f(x: i32) -> Foo {
	    // ...
	}

一种特殊的注释是 ``// SAFETY:`` 注释。这些注释必须出现在每个 ``unsafe`` 块之前，它们
解释了为什么该块内的代码是正确/健全的，即为什么它在任何情况下都不会触发未定义行为，例如:

.. code-block:: rust

	// SAFETY: `p` is valid by the safety requirements.
	unsafe { *p = 0; }

``// SAFETY:`` 注释不能与代码文档中的 ``# Safety`` 部分相混淆。 ``# Safety`` 部
分指定了（函数）调用者或（特性）实现者需要遵守的契约。
``// SAFETY:`` 注释显示了为什么一个（函数）调用者或（特性）实现者实际上尊重了
``# Safety`` 部分或语言参考中的前提条件。


代码文档
--------

Rust内核代码不像C内核代码那样被记录下来（即通过kernel-doc）。取而代之的是用于记录Rust
代码的常用系统：rustdoc工具，它使用Markdown（一种轻量级的标记语言）。

要学习Markdown，外面有很多指南。例如：

https://commonmark.org/help/

一个记录良好的Rust函数可能是这样的:

.. code-block:: rust

	/// Returns the contained [`Some`] value, consuming the `self` value,
	/// without checking that the value is not [`None`].
	///
	/// # Safety
	///
	/// Calling this method on [`None`] is *[undefined behavior]*.
	///
	/// [undefined behavior]: https://doc.rust-lang.org/reference/behavior-considered-undefined.html
	///
	/// # Examples
	///
	/// ```
	/// let x = Some("air");
	/// assert_eq!(unsafe { x.unwrap_unchecked() }, "air");
	/// ```
	pub unsafe fn unwrap_unchecked(self) -> T {
	    match self {
	        Some(val) => val,

	        // SAFETY: The safety contract must be upheld by the caller.
	        None => unsafe { hint::unreachable_unchecked() },
	    }
	}

这个例子展示了一些 ``rustdoc`` 的特性和内核中遵循的一些惯例:

  - 第一段必须是一个简单的句子，简要地描述被记录的项目的作用。进一步的解释必须放在额
    外的段落中。

  - 不安全的函数必须在 ``# Safety`` 部分记录其安全前提条件。

  - 虽然这里没有显示，但如果一个函数可能会恐慌，那么必须在 ``# Panics`` 部分描述发
    生这种情况的条件。

    请注意，恐慌应该是非常少见的，只有在有充分理由的情况下才会使用。几乎在所有的情况下，
    都应该使用一个可失败的方法，通常是返回一个 ``Result``。

  - 如果提供使用实例对读者有帮助的话，必须写在一个叫做``# Examples``的部分。

  - Rust项目（函数、类型、常量……）必须有适当的链接(``rustdoc`` 会自动创建一个
    链接)。

  - 任何 ``unsafe`` 的代码块都必须在前面加上一个 ``// SAFETY:`` 的注释，描述里面
    的代码为什么是正确的。

    虽然有时原因可能看起来微不足道，但写这些注释不仅是记录已经考虑到的问题的好方法，
    最重要的是，它提供了一种知道没有额外隐含约束的方法。

要了解更多关于如何编写Rust和拓展功能的文档，请看看 ``rustdoc`` 这本书，网址是:

	https://doc.rust-lang.org/rustdoc/how-to-write-documentation.html

此外，内核支持通过在链接目标前添加 ``srctree/`` 来创建相对于源代码树的链接。例如:

.. code-block:: rust

       //! C header: [`include/linux/printk.h`](srctree/include/linux/printk.h)

或者:

.. code-block:: rust

       /// [`struct mutex`]: srctree/include/linux/mutex.h


命名
----

Rust内核代码遵循通常的Rust命名空间:

	https://rust-lang.github.io/api-guidelines/naming.html

当现有的C语言概念（如宏、函数、对象......）被包装成Rust抽象时，应该使用尽可能接近C语
言的名称，以避免混淆，并在C语言和Rust语言之间来回切换时提高可读性。例如，C语言中的
``pr_info`` 这样的宏在Rust中的命名是一样的。

说到这里，应该调整大小写以遵循Rust的命名惯例，模块和类型引入的命名间隔不应该在项目名称
中重复。例如，在包装常量时，如:

.. code-block:: c

	#define GPIO_LINE_DIRECTION_IN	0
	#define GPIO_LINE_DIRECTION_OUT	1

在Rust中的等价物可能是这样的（忽略文档）。:

.. code-block:: rust

	pub mod gpio {
	    pub enum LineDirection {
	        In = bindings::GPIO_LINE_DIRECTION_IN as _,
	        Out = bindings::GPIO_LINE_DIRECTION_OUT as _,
	    }
	}

也就是说， ``GPIO_LINE_DIRECTION_IN`` 的等价物将被称为 ``gpio::LineDirection::In`` 。
特别是，它不应该被命名为 ``gpio::gpio_line_direction::GPIO_LINE_DIRECTION_IN`` 。

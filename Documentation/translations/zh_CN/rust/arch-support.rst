.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/rust/arch-support.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

架构支持
========

目前，Rust编译器（``rustc``）使用LLVM进行代码生成，这限制了可以支持的目标架构。此外，对
使用LLVM/Clang构建内核的支持也有所不同（请参见 Documentation/kbuild/llvm.rst ）。这
种支持对于使用 ``libclang`` 的 ``bindgen`` 来说是必需的。

下面是目前可以工作的架构的一般总结。支持程度与 ``MAINTAINERS`` 文件中的``S`` 值相对应:

=============  ================  ==============================================
架构           支持水平           限制因素
=============  ================  ==============================================
``arm64``      Maintained        只有小端序
``loongarch``  Maintained        \-
``riscv``      Maintained        只有 ``riscv64``
``um``         Maintained        只有 ``x86_64``
``x86``        Maintained        只有 ``x86_64``
=============  ================  ==============================================

.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/rust/index.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

Rust
====

与内核中的Rust有关的文档。若要开始在内核中使用Rust，请阅读 quick-start.rst 指南。

Rust 实验
---------
Rust 支持在 v6.1 版本中合并到主线，以帮助确定 Rust 作为一种语言是否适合内核，
即是否值得进行权衡。

目前，Rust 支持主要面向对 Rust 支持感兴趣的内核开发人员和维护者，
以便他们可以开始处理抽象和驱动程序，并帮助开发基础设施和工具。

如果您是终端用户，请注意，目前没有适合或旨在生产使用的内置驱动程序或模块，
并且 Rust 支持仍处于开发/实验阶段，尤其是对于特定内核配置。

代码文档
--------

给定一个内核配置，内核可能会生成 Rust 代码文档，即由 ``rustdoc`` 工具呈现的 HTML。

.. only:: rustdoc and html

   该内核文档使用 `Rust 代码文档 <rustdoc/kernel/index.html>`_ 构建。

.. only:: not rustdoc and html

   该内核文档不使用 Rust 代码文档构建。

预生成版本提供在：https://rust.docs.kernel.org。

请参阅 :ref:`代码文档 <rust_code_documentation_zh_cn>` 部分以获取更多详细信息。

.. toctree::
    :maxdepth: 1

    quick-start
    general-information
    coding-guidelines
    arch-support
    testing

你还可以在 :doc:`../../../process/kernel-docs` 中找到 Rust 的学习材料。

.. only::  subproject and html

   Indices
   =======

   * :ref:`genindex`

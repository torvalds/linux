.. include:: ../disclaimer-zh_CN.rst

:Original: :ref:`Documentation/process/programming-language.rst <programming_language>`
:Translator: Alex Shi <alex.shi@linux.alibaba.com>

程序设计语言
============

内核是用 C 编程语言编写的 [zh_cn_c-language]_。更准确地说，内核通常使用 ``gcc`` [zh_cn_gcc]_ 编译，
并且使用 ``-std=gnu11`` [zh_cn_gcc-c-dialect-options]_：这是 ISO C11 的 GNU 方言。
``clang`` [zh_cn_clang]_ 也得到了支持，详见文档：
:ref:`使用 Clang/LLVM 构建 Linux <kbuild_llvm>`。

这种方言包含对 C 语言的许多扩展 [zh_cn_gnu-extensions]_，当然，它们许多都在内核中使用。

属性
----

在整个内核中使用的一个常见扩展是属性（attributes） [zh_cn_gcc-attribute-syntax]_。
属性允许将实现定义的语义引入语言实体（如变量、函数或类型），而无需对语言进行
重大的语法更改（例如添加新关键字） [zh_cn_n2049]_。

在某些情况下，属性是可选的（即不支持这些属性的编译器仍然应该生成正确的代码，
即使其速度较慢或执行的编译时检查/诊断次数不够）

内核定义了伪关键字（例如， ``pure`` ），而不是直接使用GNU属性语法（例如,
``__attribute__((__pure__))`` ），以检测可以使用哪些关键字和/或缩短代码, 具体
请参阅 ``include/linux/compiler_attributes.h``

Rust
----

内核对 Rust 编程语言 [zh_cn_rust-language]_ 的支持是实验性的，并且可以通过配置选项
``CONFIG_RUST`` 来启用。Rust 代码使用 ``rustc`` [zh_cn_rustc]_ 编译器在
``--edition=2021`` [zh_cn_rust-editions]_ 选项下进行编译。版本（Editions）是一种
在语言中引入非后向兼容的小型变更的方式。

除此之外，内核中还使用了一些不稳定的特性 [zh_cn_rust-unstable-features]_。这些不稳定
的特性将来可能会发生变化，因此，一个重要的目标是达到仅使用稳定特性的程度。

具体请参阅 Documentation/rust/index.rst

.. [zh_cn_c-language] http://www.open-std.org/jtc1/sc22/wg14/www/standards
.. [zh_cn_gcc] https://gcc.gnu.org
.. [zh_cn_clang] https://clang.llvm.org
.. [zh_cn_gcc-c-dialect-options] https://gcc.gnu.org/onlinedocs/gcc/C-Dialect-Options.html
.. [zh_cn_gnu-extensions] https://gcc.gnu.org/onlinedocs/gcc/C-Extensions.html
.. [zh_cn_gcc-attribute-syntax] https://gcc.gnu.org/onlinedocs/gcc/Attribute-Syntax.html
.. [zh_cn_n2049] http://www.open-std.org/jtc1/sc22/wg14/www/docs/n2049.pdf
.. [zh_cn_rust-language] https://www.rust-lang.org
.. [zh_cn_rustc] https://doc.rust-lang.org/rustc/
.. [zh_cn_rust-editions] https://doc.rust-lang.org/edition-guide/editions/
.. [zh_cn_rust-unstable-features] https://github.com/Rust-for-Linux/linux/issues/2

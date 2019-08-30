.. include:: ../disclaimer-zh_CN.rst

:Original: :ref:`Documentation/process/programming-language.rst <programming_language>`
:Translator: Alex Shi <alex.shi@linux.alibaba.com>

.. _cn_programming_language:

程序设计语言
============

内核是用C语言 [c-language]_ 编写的。更准确地说，内核通常是用 ``gcc`` [gcc]_
在 ``-std=gnu89`` [gcc-c-dialect-options]_ 下编译的：ISO C90的 GNU 方言（
包括一些C99特性）

这种方言包含对语言 [gnu-extensions]_ 的许多扩展，当然，它们许多都在内核中使用。

对于一些体系结构，有一些使用 ``clang`` [clang]_ 和 ``icc`` [icc]_ 编译内核
的支持，尽管在编写此文档时还没有完成，仍需要第三方补丁。

属性
----

在整个内核中使用的一个常见扩展是属性（attributes） [gcc-attribute-syntax]_
属性允许将实现定义的语义引入语言实体（如变量、函数或类型），而无需对语言进行
重大的语法更改（例如添加新关键字） [n2049]_

在某些情况下，属性是可选的（即不支持这些属性的编译器仍然应该生成正确的代码，
即使其速度较慢或执行的编译时检查/诊断次数不够）

内核定义了伪关键字（例如， ``pure`` ），而不是直接使用GNU属性语法（例如,
``__attribute__((__pure__))`` ），以检测可以使用哪些关键字和/或缩短代码, 具体
请参阅 ``include/linux/compiler_attributes.h``

.. [c-language] http://www.open-std.org/jtc1/sc22/wg14/www/standards
.. [gcc] https://gcc.gnu.org
.. [clang] https://clang.llvm.org
.. [icc] https://software.intel.com/en-us/c-compilers
.. [gcc-c-dialect-options] https://gcc.gnu.org/onlinedocs/gcc/C-Dialect-Options.html
.. [gnu-extensions] https://gcc.gnu.org/onlinedocs/gcc/C-Extensions.html
.. [gcc-attribute-syntax] https://gcc.gnu.org/onlinedocs/gcc/Attribute-Syntax.html
.. [n2049] http://www.open-std.org/jtc1/sc22/wg14/www/docs/n2049.pdf

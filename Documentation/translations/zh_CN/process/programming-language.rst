.. include:: ../disclaimer-zh_CN.rst

:Original: :ref:`Documentation/process/programming-language.rst <programming_language>`
:Translator: Alex Shi <alex.shi@linux.alibaba.com>

.. _cn_programming_language:

程序设计语言
============

内核是用C语言 :ref:`c-language <cn_c-language>` 编写的。更准确地说，内核通常是用 :ref:`gcc <cn_gcc>`
在 ``-std=gnu89`` :ref:`gcc-c-dialect-options <cn_gcc-c-dialect-options>` 下编译的：ISO C90的 GNU 方言（
包括一些C99特性）

这种方言包含对语言 :ref:`gnu-extensions <cn_gnu-extensions>` 的许多扩展，当然，它们许多都在内核中使用。

对于一些体系结构，有一些使用 :ref:`clang <cn_clang>` 和 :ref:`icc <cn_icc>` 编译内核
的支持，尽管在编写此文档时还没有完成，仍需要第三方补丁。

属性
----

在整个内核中使用的一个常见扩展是属性（attributes） :ref:`gcc-attribute-syntax <cn_gcc-attribute-syntax>`
属性允许将实现定义的语义引入语言实体（如变量、函数或类型），而无需对语言进行
重大的语法更改（例如添加新关键字） :ref:`n2049 <cn_n2049>`

在某些情况下，属性是可选的（即不支持这些属性的编译器仍然应该生成正确的代码，
即使其速度较慢或执行的编译时检查/诊断次数不够）

内核定义了伪关键字（例如， ``pure`` ），而不是直接使用GNU属性语法（例如,
``__attribute__((__pure__))`` ），以检测可以使用哪些关键字和/或缩短代码, 具体
请参阅 ``include/linux/compiler_attributes.h``

.. _cn_c-language:

c-language
   http://www.open-std.org/jtc1/sc22/wg14/www/standards

.. _cn_gcc:

gcc
   https://gcc.gnu.org

.. _cn_clang:

clang
   https://clang.llvm.org

.. _cn_icc:

icc
   https://software.intel.com/en-us/c-compilers

.. _cn_gcc-c-dialect-options:

c-dialect-options
   https://gcc.gnu.org/onlinedocs/gcc/C-Dialect-Options.html

.. _cn_gnu-extensions:

gnu-extensions
   https://gcc.gnu.org/onlinedocs/gcc/C-Extensions.html

.. _cn_gcc-attribute-syntax:

gcc-attribute-syntax
   https://gcc.gnu.org/onlinedocs/gcc/Attribute-Syntax.html

.. _cn_n2049:

n2049
   http://www.open-std.org/jtc1/sc22/wg14/www/docs/n2049.pdf

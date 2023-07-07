.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: :ref:`Documentation/process/programming-language.rst <programming_language>`
:Translator: Alex Shi <alex.shi@linux.alibaba.com>
             Hu Haowen <src.res.211@gmail.com>

.. _tw_programming_language:

程序設計語言
============

內核是用C語言 :ref:`c-language <tw_c-language>` 編寫的。更準確地說，內核通常是用 :ref:`gcc <tw_gcc>`
在 ``-std=gnu11`` :ref:`gcc-c-dialect-options <tw_gcc-c-dialect-options>` 下編譯的：ISO C11的 GNU 方言

這種方言包含對語言 :ref:`gnu-extensions <tw_gnu-extensions>` 的許多擴展，當然，它們許多都在內核中使用。

對於一些體系結構，有一些使用 :ref:`clang <tw_clang>` 和 :ref:`icc <tw_icc>` 編譯內核
的支持，儘管在編寫此文檔時還沒有完成，仍需要第三方補丁。

屬性
----

在整個內核中使用的一個常見擴展是屬性（attributes） :ref:`gcc-attribute-syntax <tw_gcc-attribute-syntax>`
屬性允許將實現定義的語義引入語言實體（如變量、函數或類型），而無需對語言進行
重大的語法更改（例如添加新關鍵字） :ref:`n2049 <tw_n2049>`

在某些情況下，屬性是可選的（即不支持這些屬性的編譯器仍然應該生成正確的代碼，
即使其速度較慢或執行的編譯時檢查/診斷次數不夠）

內核定義了僞關鍵字（例如， ``pure`` ），而不是直接使用GNU屬性語法（例如,
``__attribute__((__pure__))`` ），以檢測可以使用哪些關鍵字和/或縮短代碼, 具體
請參閱 ``include/linux/compiler_attributes.h``

.. _tw_c-language:

c-language
   http://www.open-std.org/jtc1/sc22/wg14/www/standards

.. _tw_gcc:

gcc
   https://gcc.gnu.org

.. _tw_clang:

clang
   https://clang.llvm.org

.. _tw_icc:

icc
   https://software.intel.com/en-us/c-compilers

.. _tw_gcc-c-dialect-options:

c-dialect-options
   https://gcc.gnu.org/onlinedocs/gcc/C-Dialect-Options.html

.. _tw_gnu-extensions:

gnu-extensions
   https://gcc.gnu.org/onlinedocs/gcc/C-Extensions.html

.. _tw_gcc-attribute-syntax:

gcc-attribute-syntax
   https://gcc.gnu.org/onlinedocs/gcc/Attribute-Syntax.html

.. _tw_n2049:

n2049
   http://www.open-std.org/jtc1/sc22/wg14/www/docs/n2049.pdf


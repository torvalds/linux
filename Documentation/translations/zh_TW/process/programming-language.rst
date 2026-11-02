.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: :ref:`Documentation/process/programming-language.rst <programming_language>`
:Translator: Alex Shi <alex.shi@linux.alibaba.com>
             Hu Haowen <2023002089@link.tyut.edu.cn>
             Chen-Yu Yeh <chenyou910331@gmail.com>

.. _tw_programming_language:

程式語言
========

Linux核心是用C程式語言 [zh_tw_c-language]_ 編寫的。更準確地說，核心通常使
用``gcc`` [zh_tw_gcc]_ 編譯，並且使用 ``-std=gnu11``
[zh_tw_gcc-c-dialect-options]_：這是 ISO C11 的 GNU 方言。``clang``
[zh_tw_clang]_ 也得到了支援，詳見文件：
:ref:`使用 Clang/LLVM 建置 Linux <kbuild_llvm>`。

這種方言包含對C語言的許多擴展 [zh_tw_gnu-extensions]_，當然，它們許多都在核心
中使用。

屬性
----

在整個核心中使用的一個常見擴展是屬性（attributes）
[zh_tw_gcc-attribute-syntax]_。屬性允許將實作定義的語義引入語言實體（如變
數、函式或型別），而無需對語言進行重大的語法更改（例如添加新關鍵字）
[zh_tw_n2049]_。

在某些情況下，屬性是可選的（即不支援這些屬性的編譯器仍然應該產生正確的程式碼，
即使其速度較慢或執行的編譯時檢查/診斷次數不夠）。

核心定義了偽關鍵字（例如， ``__pure`` ），而不是直接使用GNU屬性語法（例如,
``__attribute__((__pure__))`` ），以檢測可以使用哪些關鍵字和/或縮短程式碼，
具體請參閱 ``include/linux/compiler_attributes.h``

Rust
----

核心支援 Rust 程式語言 [zh_tw_rust-language]_，並可以透過設定選項
``CONFIG_RUST`` 來啟用。Rust 程式碼使用 ``rustc`` [zh_tw_rustc]_ 編譯器在
``--edition=2021`` [zh_tw_rust-editions]_ 選項下進行編譯。版本（Editions）是
一種在語言中引入非後向相容的小型變更的方式。

除此之外，核心中還使用了一些不穩定的特性 [zh_tw_rust-unstable-features]_。
這些不穩定的特性將來可能會發生變化，因此，一個重要的目標是達到僅使用穩定特性
的程度。

具體請參閱 Documentation/rust/index.rst

.. [zh_tw_c-language] http://www.open-std.org/jtc1/sc22/wg14/www/standards
.. [zh_tw_gcc] https://gcc.gnu.org
.. [zh_tw_clang] https://clang.llvm.org
.. [zh_tw_gcc-c-dialect-options] https://gcc.gnu.org/onlinedocs/gcc/C-Dialect-Options.html
.. [zh_tw_gnu-extensions] https://gcc.gnu.org/onlinedocs/gcc/C-Extensions.html
.. [zh_tw_gcc-attribute-syntax] https://gcc.gnu.org/onlinedocs/gcc/Attribute-Syntax.html
.. [zh_tw_n2049] http://www.open-std.org/jtc1/sc22/wg14/www/docs/n2049.pdf
.. [zh_tw_rust-language] https://www.rust-lang.org
.. [zh_tw_rustc] https://doc.rust-lang.org/rustc/
.. [zh_tw_rust-editions] https://doc.rust-lang.org/edition-guide/editions/
.. [zh_tw_rust-unstable-features] https://github.com/Rust-for-Linux/linux/issues/2

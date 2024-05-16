.. raw:: latex

	\renewcommand\thesection*
	\renewcommand\thesubsection*

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/process/index.rst

:翻译:

 Alex Shi <alex.shi@linux.alibaba.com>

========================
与Linux 内核社区一起工作
========================

你想成为Linux内核开发人员吗？欢迎之至！在学习许多关于内核的技术知识的同时，
了解我们社区的工作方式也很重要。阅读这些文档可以让您以更轻松的、麻烦更少的
方式将更改合并到内核。

以下是每位开发人员都应阅读的基本指南：

.. toctree::
   :maxdepth: 1

   license-rules
   howto
   code-of-conduct
   code-of-conduct-interpretation
   development-process
   submitting-patches
   programming-language
   coding-style
   maintainer-pgp-guide
   email-clients
   kernel-enforcement-statement
   kernel-driver-statement

TODOLIST:

* handling-regressions
* maintainer-handbooks

安全方面, 请阅读:

.. toctree::
   :maxdepth: 1

   embargoed-hardware-issues

TODOLIST:

* security-bugs

其它大多数开发人员感兴趣的社区指南：


.. toctree::
   :maxdepth: 1

   stable-api-nonsense
   management-style
   stable-kernel-rules
   submit-checklist

TODOLIST:

* changes
* kernel-docs
* deprecated
* maintainers
* researcher-guidelines
* contribution-maturity-model


这些是一些总体性技术指南，由于不大好分类而放在这里：

.. toctree::
   :maxdepth: 1

   magic-number
   volatile-considered-harmful
   ../arch/riscv/patch-acceptance
   ../core-api/unaligned-memory-access

TODOLIST:

* applying-patches
* backporting
* adding-syscalls
* botching-up-ioctls
* clang-format

.. only::  subproject and html

   目录
   ====

   * :ref:`genindex`

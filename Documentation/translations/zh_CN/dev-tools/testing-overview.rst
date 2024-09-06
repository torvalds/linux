.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/dev-tools/testing-overview.rst
:Translator: 胡皓文 Hu Haowen <2023002089@link.tyut.edu.cn>

============
内核测试指南
============

有许多不同的工具可以用于测试Linux内核，因此了解什么时候使用它们可能
很困难。本文档粗略概述了它们之间的区别，并阐释了它们是怎样糅合在一起
的。

编写和运行测试
==============

大多数内核测试都是用kselftest或KUnit框架之一编写的。它们都让运行测试
更加简化，并为编写新测试提供帮助。

如果你想验证内核的行为——尤其是内核的特定部分——那你就要使用kUnit或
kselftest。

KUnit和kselftest的区别
----------------------

.. note::
     由于本文段中部分术语尚无较好的对应中文释义，可能导致与原文含义
     存在些许差异，因此建议读者结合原文
     （Documentation/dev-tools/testing-overview.rst）辅助阅读。
     如对部分翻译有异议或有更好的翻译意见，欢迎联系译者进行修订。

KUnit（Documentation/dev-tools/kunit/index.rst）是用于“白箱”测
试的一个完整的内核内部系统：因为测试代码是内核的一部分，所以它能够访
问用户空间不能访问到的内部结构和功能。

因此，KUnit测试最好针对内核中较小的、自包含的部分，以便能够独立地测
试。“单元”测试的概念亦是如此。

比如，一个KUnit测试可能测试一个单独的内核功能（甚至通过一个函数测试
一个单一的代码路径，例如一个错误处理案例），而不是整个地测试一个特性。

这也使得KUnit测试构建和运行非常地快，从而能够作为开发流程的一部分被
频繁地运行。

有关更详细的介绍，请参阅KUnit测试代码风格指南
Documentation/dev-tools/kunit/style.rst

kselftest（Documentation/dev-tools/kselftest.rst），相对来说，大量用
于用户空间，并且通常测试用户空间的脚本或程序。

这使得编写复杂的测试，或者需要操作更多全局系统状态的测试更加容易（诸
如生成进程之类）。然而，从kselftest直接调用内核函数是不行的。这也就
意味着只有通过某种方式（如系统调用、驱动设备、文件系统等）导出到了用
户空间的内核功能才能使用kselftest来测试。为此，有些测试包含了一个伴
生的内核模块用于导出更多的信息和功能。不过，对于基本上或者完全在内核
中运行的测试，KUnit可能是更佳工具。

kselftest也因此非常适合于全部功能的测试，因为这些功能会将接口暴露到
用户空间，从而能够被测试，而不是展现实现细节。“system”测试和
“end-to-end”测试亦是如此。

比如，一个新的系统调用应该伴随有新的kselftest测试。

代码覆盖率工具
==============

支持两种不同代码之间的覆盖率测量工具。它们可以用来验证一项测试执行的
确切函数或代码行。这有助于决定内核被测试了多少，或用来查找合适的测试
中没有覆盖到的极端情况。

Documentation/translations/zh_CN/dev-tools/gcov.rst 是GCC的覆盖率测试
工具，能用于获取内核的全局或每个模块的覆盖率。与KCOV不同的是，这个工具
不记录每个任务的覆盖率。覆盖率数据可以通过debugfs读取，并通过常规的
gcov工具进行解释。

Documentation/dev-tools/kcov.rst 是能够构建在内核之中，用于在每个任务
的层面捕捉覆盖率的一个功能。因此，它对于模糊测试和关于代码执行期间信
息的其它情况非常有用，比如在一个单一系统调用里使用它就很有用。

动态分析工具
============

内核也支持许多动态分析工具，用以检测正在运行的内核中出现的多种类型的
问题。这些工具通常每个去寻找一类不同的缺陷，比如非法内存访问，数据竞
争等并发问题，或整型溢出等其他未定义行为。

如下所示：

* kmemleak检测可能的内存泄漏。参阅
  Documentation/dev-tools/kmemleak.rst
* KASAN检测非法内存访问，如数组越界和释放后重用（UAF）。参阅
  Documentation/dev-tools/kasan.rst
* UBSAN检测C标准中未定义的行为，如整型溢出。参阅
  Documentation/dev-tools/ubsan.rst
* KCSAN检测数据竞争。参阅 Documentation/dev-tools/kcsan.rst
* KFENCE是一个低开销的内存问题检测器，比KASAN更快且能被用于批量构建。
  参阅 Documentation/dev-tools/kfence.rst
* lockdep是一个锁定正确性检测器。参阅
  Documentation/locking/lockdep-design.rst
* 运行时确认（Runtime Verification）支持检查给定子系统的特定行为。参阅
  Documentation/trace/rv/runtime-verification.rst。
* 除此以外，在内核中还有一些其它的调试工具，大多数能在
  lib/Kconfig.debug 中找到。

这些工具倾向于对内核进行整体测试，并且不像kselftest和KUnit一样“传递”。
它们可以通过在启用这些工具时运行内核测试以与kselftest或KUnit结合起来：
之后你就能确保这些错误在测试过程中都不会发生了。

一些工具与KUnit和kselftest集成，并且在检测到问题时会自动打断测试。

静态分析工具
============

除了测试运行中的内核，我们还可以使用**静态分析**工具直接分析内核的源代
码（**在编译时**）。内核中常用的工具允许人们检查整个源代码树或其中的特
定文件。它们使得在开发过程中更容易发现和修复问题。

 Sparse可以通过执行类型检查、锁检查、值范围检查来帮助测试内核，此外还
 可以在检查代码时报告各种错误和警告。关于如何使用它的细节，请参阅
 Documentation/translations/zh_CN/dev-tools/sparse.rst。

 Smatch扩展了Sparse，并提供了对编程逻辑错误的额外检查，如开关语句中
 缺少断点，错误检查中未使用的返回值，忘记在错误路径的返回中设置错误代
 码等。Smatch也有针对更严重问题的测试，如整数溢出、空指针解除引用和内
 存泄漏。见项目页面http://smatch.sourceforge.net/。

 Coccinelle是我们可以使用的另一个静态分析器。Coccinelle经常被用来
 帮助源代码的重构和并行演化，但它也可以帮助避免常见代码模式中出现的某
 些错误。可用的测试类型包括API测试、内核迭代器的正确使用测试、自由操
 作的合理性检查、锁定行为的分析，以及已知的有助于保持内核使用一致性的
 进一步测试。详情请见Documentation/dev-tools/coccinelle.rst。

 不过要注意的是，静态分析工具存在**假阳性**的问题。在试图修复错误和警
 告之前，需要仔细评估它们。

何时使用Sparse和Smatch
----------------------

Sparse做类型检查，例如验证注释的变量不会导致无符号的错误，检测
``__user`` 指针使用不当的地方，以及分析符号初始化器的兼容性。

Smatch进行流程分析，如果允许建立函数数据库，它还会进行跨函数分析。
Smatch试图回答一些问题，比如这个缓冲区是在哪里分配的？它有多大？这
个索引可以由用户控制吗？这个变量比那个变量大吗？

一般来说，在Smatch中写检查比在Sparse中写检查要容易。尽管如此，
Sparse和Smatch的检查还是有一些重叠的地方。

Smatch和Coccinelle的强项
------------------------

Coccinelle可能是最容易写检查的。它在预处理器之前工作，所以用Coccinelle
检查宏中的错误更容易。Coccinelle还能为你创建补丁，这是其他工具无法做到的。

例如，用Coccinelle你可以从 ``kmalloc_array(x, size, GFP_KERNEL)``
到 ``kmalloc_array(x, size, GFP_KERNEL)`` 进行大规模转换，这真的很
有用。如果你只是创建一个Smatch警告，并试图把转换的工作推给维护者，他们会很
恼火。你将不得不为每个警告争论是否真的可以溢出。

Coccinelle不对变量值进行分析，而这正是Smatch的强项。另一方面，Coccinelle
允许你用简单的方法做简单的事情。

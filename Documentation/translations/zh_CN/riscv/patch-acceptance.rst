.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/riscv/patch-acceptance.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

.. _cn_riscv_patch-acceptance:

arch/riscv 开发者维护指南
=========================

概述
----
RISC-V指令集体系结构是公开开发的：
正在进行的草案可供所有人查看和测试实现。新模块或者扩展草案可能会在开发过程中发
生更改---有时以不兼容的方式对以前的草案进行更改。这种灵活性可能会给RISC-V Linux
维护者带来挑战。Linux开发过程更喜欢经过良好检查和测试的代码，而不是试验代码。我
们希望推广同样的规则到即将被内核合并的RISC-V相关代码。

附加的提交检查单
----------------
我们仅接受相关标准已经被RISC-V基金会标准为“已批准”或“已冻结”的扩展或模块的补丁。
（开发者当然可以维护自己的Linux内核树，其中包含所需代码扩展草案的代码。）

此外，RISC-V规范允许爱好者创建自己的自定义扩展。这些自定义拓展不需要通过RISC-V
基金会的任何审核或批准。为了避免将爱好者一些特别的RISC-V拓展添加进内核代码带来
的维护复杂性和对性能的潜在影响，我们将只接受RISC-V基金会正式冻结或批准的的扩展
补丁。（开发者当然可以维护自己的Linux内核树，其中包含他们想要的任何自定义扩展
的代码。）

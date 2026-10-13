.. SPDX-License-Identifier: GPL-2.0

.. include:: ../../disclaimer-zh_CN.rst

:Original: Documentation/arch/riscv/patch-acceptance.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

.. _cn_riscv_patch-acceptance:

arch/riscv 开发者维护指南
=========================

概述
----
RISC-V 指令集体系结构是公开开发的：
正在进行的草案可供所有人查看和测试实现。新模块或者扩展草案可能会在开发过程中发
生更改 --- 有时以不兼容的方式对以前的草案进行更改。这种灵活性可能会给 RISC-V
Linux 维护者带来挑战。Linux 维护者不赞成频繁的变更，且 Linux 开发过程更喜欢经过
良好检查和测试的代码，而不是试验代码。我们希望推广同样的规则到即将被内核合并的
RISC-V 相关代码。

Patchwork
---------

RISC-V 有一个 patchwork 实例，可以在那里查看补丁的状态：

  https://patchwork.kernel.org/project/linux-riscv/list/

如果你的补丁不在默认视图中出现，那么 RISC-V 维护者很有可能已要求修改，或者希望
将其应用到另一个代码树上。

自动化流程会在该 patchwork 实例上运行，在每个补丁到达时立刻对其进行构建/测试。
自动化流程会根据补丁是否被识别为修复，选用 RISC-V `for-next` 或 `fixes` 分支
当前的 HEAD；若上述均应用失败，则使用 RISC-V `master` 分支。补丁系列被应用到的具
体提交将标注在 patchwork 上。任何检查未通过的补丁通常不会被应用，并且在大多数情
况下将需要重新提交。

附加的提交检查单
----------------
我们仅接受针对新模块或扩展的补丁，前提是这些模块或扩展的规范被列为未来不太可能发
生不兼容的变更。对于来自 RISC-V 基金会的规范，这意味着“已冻结”或“已批准”，对于
UEFI 论坛的规范，这意味着已发布的 ECR。（开发者当然可以维护自己的 Linux 内核树，
其中包含他们所需的任何扩展草案的代码。）

此外，RISC-V 规范允许实现者创建自己的自定义扩展。这些自定义扩展不需要通过 RISC-V
基金会的任何审核或批准流程。为了避免因添加实现者特定的 RISC-V 扩展带来的维护复杂
性和对性能的潜在影响，我们将只考虑符合以下任一条件的扩展补丁：

- 已由 RISC-V 基金会正式冻结或批准
- 已按照标准 Linux 惯例，在广泛可用的硬件中实现

（实现者当然可以维护自己的 Linux 内核树，其中包含他们所需的任何自定义扩展的代码。）

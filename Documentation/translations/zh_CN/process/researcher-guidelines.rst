.. SPDX-License-Identifier: GPL-2.0-or-later

.. include:: ../disclaimer-zh_CN.rst

.. _cn_researcherguidelines:

:Original: Documentation/process/researcher-guidelines.rst

:译者:
 - 慕冬亮 Dongliang Mu <dzm91@hust.edu.cn>

研究人员指南
+++++++++++++++++++++

Linux 内核社区欢迎对 Linux 内核及其开发过程中涉及的活动与任何其他副产品
进行透明的研究。Linux 从这种研究中受益匪浅，其多方面均由某种形式的研究所推动。

社区非常感谢研究人员在公开研究结果之前能分享初步发现，特别是涉及安全的研究。
早期参与有助于提高研究质量并使 Linux 受益。无论如何，推荐研究人员与社区分享
已发表研究的开放访问副本。

本文旨在澄清研究开展过程中 Linux 内核社区认可与不认可的一些做法。至少，这类
研究及相关活动应遵循标准的研究伦理规则。有关研究伦理、技术伦理以及开发者社区
研究的更多背景信息，请查阅：

* `研究伦理史 <https://www.unlv.edu/research/ORI-HSR/history-ethics>`_
* `IEEE 伦理 <https://www.ieee.org/about/ethics/index.html>`_
* `开发者和研究人员对开源项目实验伦理的看法 <https://arxiv.org/pdf/2112.13217.pdf>`_

Linux 内核社区期望与项目互动的每个人都是真诚地为了使 Linux 变得更好。
对 Linux 内核社区产生的任何公开可用的成果（包括但不限于源代码）的研究
是受欢迎的，对开发者的研究如若要开展，则必须要明确说明，获得（开发者）同意。

完全基于公开可用资源（包括公共邮件列表的帖子和公开代码库的提交）的被动研究
显然是允许的。不过，和任何研究一样，仍需遵循标准伦理。

然而，针对开发者行为的主动研究必须在获得相关开发者的明确同意和完全披露的情况下进行。
未经同意，不得与开发者互动或对其进行实验；这也是标准的研究伦理。

调查
=======

研究通常采用调查问卷的形式发送给维护者或贡献者。然而，内核社区通常从这些调查问卷中获益
甚少。内核开发过程之所以有效，是因为每个开发者都从中受益，即使与目标不同的人一起工作。
而回应调查则是对繁忙开发者的单向需求，对他们自己或整个内核社区没有相应的好处。因此，
这种研究方法不被鼓励。

内核社区成员已经收到过多的电子邮件，可能会将调查请求视为对他们时间的又一要求。发送
此类请求会剥夺社区宝贵的贡献者时间，且不太可能产生有统计意义的回应。

作为替代，研究人员应考虑参加开发者活动，举办研讨会来介绍研究项目及其对参与者的益处，
并直接与社区互动。该方式获得的信息将比电子邮件调查问卷丰富得多，且社区也能从中学习
到您的见解。

补丁
=======

澄清：向开发者发送补丁**是**与他们互动，但他们已经同意接收**善意贡献**。故意发送有缺陷/
有漏洞的补丁或在讨论中提供误导信息是不被同意的。这种交流会对开发者造成损害
（例如，消耗时间、精力和士气），并通过破坏整个开发者社区对贡献者（及其所在组织）
的信任而损害项目，削弱为贡献者提供建设性反馈的努力，并使最终用户面临软件缺陷的风险。

研究人员参与 Linux 本身的开发与其他人一样受到欢迎和鼓励。研究 Linux 代码是常见
做法，尤其是在开发或运行可产生可操作结果的分析工具时。

在与开发者社区互动时，发送补丁历来是产生影响的最佳方式。Linux 已经有很多已知的
漏洞 -- 更有帮助的是经过审核的修复。在贡献之前，请仔细阅读相关文档：

* Documentation/process/development-process.rst
* Documentation/process/submitting-patches.rst
* Documentation/admin-guide/reporting-issues.rst
* Documentation/process/security-bugs.rst

然后发送补丁（包括所有如下详细信息的提交日志）并跟进其他开发者的任何反馈。

当发送因研究而产生的补丁时，提交日志应至少包含以下详细信息，以便开发者有适当的上下文
来理解贡献。回答：

* 找到了什么具体问题？
* 在运行系统上如何触发这个问题？
* 遇到这个问题对系统会有什么影响？
* 如何发现这个问题？具体包括任何测试、静态或动态分析程序及其他用于执行工作的工具或方法的详细信息。
* 在哪个版本的 Linux 上发现了这个问题？强烈推荐使用最新的发布版本或最近的 linux-next 分支（参见 Documentation/process/howto.rst）。
* 进行了哪些更改来修复这个问题，为什么认为这些更改是正确的？
* 如何进行构建测试和运行时测试？
* 此更改修复了哪个先前的提交？这应该在 "Fixes:" 标签中，如文档所述。
* 还有谁审查了这个补丁？这应该在适当的 "Reviewed-by:" 标签中注明；见下文。

例如::

  From: Author <author@email>
  Subject: [PATCH] drivers/foo_bar: Add missing kfree()

  The error path in foo_bar driver does not correctly free the allocated
  struct foo_bar_info. This can happen if the attached foo_bar device
  rejects the initialization packets sent during foo_bar_probe(). This
  would result in a 64 byte slab memory leak once per device attach,
  wasting memory resources over time.

  This flaw was found using an experimental static analysis tool we are
  developing, LeakMagic[1], which reported the following warning when
  analyzing the v5.15 kernel release:

   path/to/foo_bar.c:187: missing kfree() call?

  Add the missing kfree() to the error path. No other references to
  this memory exist outside the probe function, so this is the only
  place it can be freed.

  x86_64 and arm64 defconfig builds with CONFIG_FOO_BAR=y using GCC
  11.2 show no new warnings, and LeakMagic no longer warns about this
  code path. As we don't have a FooBar device to test with, no runtime
  testing was able to be performed.

  [1] https://url/to/leakmagic/details

  Reported-by: Researcher <researcher@email>
  Fixes: aaaabbbbccccdddd ("Introduce support for FooBar")
  Signed-off-by: Author <author@email>
  Reviewed-by: Reviewer <reviewer@email>

如果您是第一次参与贡献，建议在补丁在发布到公共列表前请其他人私下进行审核。（如果明确
告诉您补丁需要更仔细的内部审查，则这是必需的。）这些人预计会在最终的补丁中包含他们的
"Reviewed-by" 标签。找到熟悉 Linux 贡献的其他开发者，特别是您自己组织内的开发者，
并在将补丁发送到公共邮件列表前请他们帮助审核，往往会显著提高补丁的质量，从而减少
其他开发者的负担。

如果你找不到人内部审核补丁且需要帮助找到这样的人，或者如果您对本文档和开发者社区的期望
有任何其他问题，请联系技术咨询委员会私有邮件列表：<tech-board@groups.linuxfoundation.org>。

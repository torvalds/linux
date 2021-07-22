.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/maintainer/modifying-patches.rst

:译者:

 吴想成 Wu XiangCheng <bobwxc@email.cn>

.. _modifyingpatches_zh:

修改补丁
========

如果你是子系统或者分支的维护者，由于代码在你的和提交者的树中并不完全相同，
有时你需要稍微修改一下收到的补丁以合并它们。

如果你严格遵守开发者来源证书的规则（c），你应该要求提交者重做，但这完全是会
适得其反的时间、精力浪费。规则（b）允许你调整代码，但这样修改提交者的代码并
让他背书你的错误是非常不礼貌的。为解决此问题，建议在你之前最后一个
Signed-off-by标签和你的之间添加一行，以指示更改的性质。这没有强制性要求，最
好在描述前面加上你的邮件和/或姓名，用方括号括住整行，以明显指出你对最后一刻
的更改负责。例如::

        Signed-off-by: Random J Developer <random@developer.example.org>
        [lucky@maintainer.example.org: struct foo moved from foo.c to foo.h]
        Signed-off-by: Lucky K Maintainer <lucky@maintainer.example.org>

如果您维护着一个稳定的分支，并希望同时明确贡献、跟踪更改、合并修复，并保护
提交者免受责难，这种做法尤其有用。请注意，在任何情况下都不得更改作者的身份
（From头），因为它会在变更日志中显示。

向后移植（back-port）人员特别要注意：为了便于跟踪，请在提交消息的顶部（即主题行
之后）插入补丁的来源，这是一种常见而有用的做法。例如，我们可以在3.x稳定版本
中看到以下内容::

        Date:   Tue Oct 7 07:26:38 2014 -0400

        libata: Un-break ATA blacklist

        commit 1c40279960bcd7d52dbdf1d466b20d24b99176c8 upstream.

下面是一个旧的内核在某补丁被向后移植后会出现的::

        Date:   Tue May 13 22:12:27 2008 +0200

        wireless, airo: waitbusy() won't delay

        [backport of 2.6 commit b7acbdfbd1f277c1eb23f344f899cfa4cd0bf36a]

不管什么格式，这些信息都为人们跟踪你的树，以及试图解决你树中的错误的人提供了
有价值的帮助。

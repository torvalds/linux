Chinese translated version of Documentation/process/stable-kernel-rules.rst

If you have any comment or update to the content, please contact the
original document maintainer directly.  However, if you have a problem
communicating in English you can also ask the Chinese maintainer for
help.  Contact the Chinese maintainer if this translation is outdated
or if there is a problem with the translation.

Chinese maintainer: TripleX Chung <triplex@zh-kernel.org>
---------------------------------------------------------------------
Documentation/process/stable-kernel-rules.rst 的中文翻译

如果想评论或更新本文的内容，请直接联系原文档的维护者。如果你使用英文
交流有困难的话，也可以向中文版维护者求助。如果本翻译更新不及时或者翻
译存在问题，请联系中文版维护者。


中文版维护者： 钟宇  TripleX Chung <triplex@zh-kernel.org>
中文版翻译者： 钟宇  TripleX Chung <triplex@zh-kernel.org>
中文版校译者： 李阳  Li Yang <leo@zh-kernel.org>
               Kangkai Yin <e12051@motorola.com>

以下为正文
---------------------------------------------------------------------

关于Linux 2.6稳定版发布，所有你想知道的事情。

关于哪些类型的补丁可以被接收进入稳定版代码树，哪些不可以的规则：

  - 必须是显而易见的正确，并且经过测试的。
  - 连同上下文，不能大于100行。
  - 必须只修正一件事情。
  - 必须修正了一个给大家带来麻烦的真正的bug（不是“这也许是一个问题...”
    那样的东西）。
  - 必须修正带来如下后果的问题：编译错误（对被标记为CONFIG_BROKEN的例外），
    内核崩溃，挂起，数据损坏，真正的安全问题，或者一些类似“哦，这不
    好”的问题。简短的说，就是一些致命的问题。
  - 没有“理论上的竞争条件”，除非能给出竞争条件如何被利用的解释。
  - 不能存在任何的“琐碎的”修正（拼写修正，去掉多余空格之类的）。
  - 必须被相关子系统的维护者接受。
  - 必须遵循Documentation/process/submitting-patches.rst里的规则。

向稳定版代码树提交补丁的过程：

  - 在确认了补丁符合以上的规则后，将补丁发送到stable@vger.kernel.org。
  - 如果补丁被接受到队列里，发送者会收到一个ACK回复，如果没有被接受，收
    到的是NAK回复。回复需要几天的时间，这取决于开发者的时间安排。
  - 被接受的补丁会被加到稳定版本队列里，等待其他开发者的审查。
  - 安全方面的补丁不要发到这个列表，应该发送到security@kernel.org。

审查周期：

  - 当稳定版的维护者决定开始一个审查周期，补丁将被发送到审查委员会，以
    及被补丁影响的领域的维护者（除非提交者就是该领域的维护者）并且抄送
    到linux-kernel邮件列表。
  - 审查委员会有48小时的时间，用来决定给该补丁回复ACK还是NAK。
  - 如果委员会中有成员拒绝这个补丁，或者linux-kernel列表上有人反对这个
    补丁，并提出维护者和审查委员会之前没有意识到的问题，补丁会从队列中
    丢弃。
  - 在审查周期结束的时候，那些得到ACK回应的补丁将会被加入到最新的稳定版
    发布中，一个新的稳定版发布就此产生。
  - 安全性补丁将从内核安全小组那里直接接收到稳定版代码树中，而不是通过
    通常的审查周期。请联系内核安全小组以获得关于这个过程的更多细节。

审查委员会：
  - 由一些自愿承担这项任务的内核开发者，和几个非志愿的组成。

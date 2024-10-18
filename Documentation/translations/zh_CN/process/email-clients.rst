.. SPDX-License-Identifier: GPL-2.0-or-later

.. include:: ../disclaimer-zh_CN.rst

.. _cn_email_clients:

:Original: Documentation/process/email-clients.rst

:译者:
 - 贾威威  Harry Wei <harryxiyou@gmail.com>
 - 时奎亮  Alex Shi <alexs@kernel.org>
 - 吴想成  Wu XiangCheng <bobwxc@email.cn>

:校译:
 - Yinglin Luan <synmyth@gmail.com>
 - Xiaochen Wang <wangxiaochen0@gmail.com>
 - yaxinsn <yaxinsn@163.com>

Linux邮件客户端配置信息
=======================

Git
---

现在大多数开发人员使用 ``git send-email`` 而不是常规的电子邮件客户端。这方面
的手册非常好。在接收端，维护人员使用 ``git am`` 加载补丁。

如果你是 ``git`` 新手，那么把你的第一个补丁发送给你自己。将其保存为包含所有
标题的原始文本。运行 ``git am raw_email.txt`` ，然后使用 ``git log`` 查看更
改日志。如果工作正常，再将补丁发送到相应的邮件列表。


通用配置
--------

Linux内核补丁是通过邮件被提交的，最好把补丁作为邮件体的内嵌文本。有些维护者
接收附件，但是附件的内容格式应该是"text/plain"。然而，附件一般是不赞成的，
因为这会使补丁的引用部分在评论过程中变的很困难。

同时也强烈建议在补丁或其他邮件的正文中使用纯文本格式。https://useplaintext.email
有助于了解如何配置你喜欢的邮件客户端，并在您还没有首选的情况下列出一些推荐的
客户端。

用来发送Linux内核补丁的邮件客户端在发送补丁时应该处于文本的原始状态。例如，
他们不能改变或者删除制表符或者空格，甚至是在每一行的开头或者结尾。

不要通过"format=flowed"模式发送补丁。这样会引起不可预期以及有害的断行。

不要让你的邮件客户端进行自动换行。这样也会破坏你的补丁。

邮件客户端不能改变文本的字符集编码方式。要发送的补丁只能是ASCII或者UTF-8编码
方式，如果你使用UTF-8编码方式发送邮件，那么你将会避免一些可能发生的字符集问题。

邮件客户端应该生成并且保持“References:”或者“In-Reply-To:”邮件头，这样邮件会话
就不会中断。

复制粘帖(或者剪贴粘帖)通常不能用于补丁，因为制表符会转换为空格。使用xclipboard,
xclip或者xcutsel也许可以，但是最好测试一下或者避免使用复制粘帖。

不要在使用PGP/GPG签名的邮件中包含补丁。这样会使得很多脚本不能读取和适用于你的
补丁。（这个问题应该是可以修复的）

在给内核邮件列表发送补丁之前，给自己发送一个补丁是个不错的主意，保存接收到的
邮件，将补丁用'patch'命令打上，如果成功了，再给内核邮件列表发送。


一些邮件客户端提示
------------------

这里给出一些详细的MUA配置提示，可以用于给Linux内核发送补丁。这些并不意味是
所有的软件包配置总结。

说明：

- TUI = 以文本为基础的用户接口
- GUI = 图形界面用户接口

Alpine (TUI)
************

配置选项：

在 :menuselection:`Sending Preferences` 菜单：

- :menuselection:`Do Not Send Flowed Text` 必须开启
- :menuselection:`Strip Whitespace Before Sending` 必须关闭

当写邮件时，光标应该放在补丁会出现的地方，然后按下 :kbd:`CTRL-R` 组合键，使指
定的补丁文件嵌入到邮件中。

Claws Mail (GUI)
****************

可以用，有人用它成功地发过补丁。

用 :menuselection:`Message-->Insert File` (:kbd:`CTRL-I`) 或外置编辑器插入补丁。

若要在Claws编辑窗口重修改插入的补丁，需关闭
:menuselection:`Configuration-->Preferences-->Compose-->Wrapping`
的 `Auto wrapping` 。

Evolution (GUI)
***************

一些开发者成功的使用它发送补丁。

撰写邮件时：
从 :menuselection:`格式-->段落样式-->预格式化` (:kbd:`CTRL-7`)
或工具栏选择 :menuselection:`预格式化` ；

然后使用：
:menuselection:`插入-->文本文件...` (:kbd:`ALT-N x`) 插入补丁文件。

你还可以 ``diff -Nru old.c new.c | xclip`` ，选择 :menuselection:`预格式化` ，
然后使用鼠标中键进行粘帖。

Kmail (GUI)
***********

一些开发者成功的使用它发送补丁。

默认撰写设置禁用HTML格式是合适的；不要启用它。

当书写一封邮件的时候，在选项下面不要选择自动换行。唯一的缺点就是你在邮件中输
入的任何文本都不会被自动换行，因此你必须在发送补丁之前手动换行。最简单的方法
就是启用自动换行来书写邮件，然后把它保存为草稿。一旦你在草稿中再次打开它，它
已经全部自动换行了，那么你的邮件虽然没有选择自动换行，但是还不会失去已有的自
动换行。

在邮件的底部，插入补丁之前，放上常用的补丁定界符：三个连字符(``---``)。

然后在 :menuselection:`信件` 菜单，选择 :menuselection:`插入文本文件` ，接
着选取你的补丁文件。还有一个额外的选项，你可以通过它配置你的创建新邮件工具栏，
加上 :menuselection:`插入文本文件` 图标。

将编辑器窗口拉到足够宽避免折行。对于KMail 1.13.5 (KDE 4.5.4)，它会在发送邮件
时对编辑器窗口中显示折行的地方自动换行。在选项菜单中取消自动换行仍不能解决。
因此，如果你的补丁中有非常长的行，必须在发送之前把编辑器窗口拉得非常宽。
参见：https://bugs.kde.org/show_bug.cgi?id=174034

你可以安全地用GPG签名附件，但是内嵌补丁最好不要使用GPG签名它们。作为内嵌文本
插入的签名补丁将使其难以从7-bit编码中提取。

如果你非要以附件的形式发送补丁，那么就右键点击附件，然后选择
:menuselection:`属性` ，打开 :menuselection:`建议自动显示` ，使附件内联更容
易让读者看到。

当你要保存将要发送的内嵌文本补丁，你可以从消息列表窗格选择包含补丁的邮件，然
后右键选择 :menuselection:`另存为` 。如果整个电子邮件的组成正确，您可直接将
其作为补丁使用。电子邮件以当前用户可读写权限保存，因此您必须 ``chmod`` ，以
使其在复制到别处时用户组和其他人可读。

Lotus Notes (GUI)
*****************

不要使用它。

IBM Verse (Web GUI)
*******************

同上条。

Mutt (TUI)
**********

很多Linux开发人员使用mutt客户端，这证明它肯定工作得非常漂亮。

Mutt不自带编辑器，所以不管你使用什么编辑器，不自动断行就行。大多数编辑器都有
:menuselection:`插入文件` 选项，它可以在不改变文件内容的情况下插入文件。

用 ``vim`` 作为mutt的编辑器::

  set editor="vi"

如果使用xclip，敲入以下命令::

  :set paste

然后再按中键或者shift-insert或者使用::

  :r filename

把补丁插入为内嵌文本。
在未设置  ``set paste`` 时(a)ttach工作的很好。

你可以通过 ``git format-patch`` 生成补丁，然后用 Mutt发送它们::

    $ mutt -H 0001-some-bug-fix.patch

配置选项：

它应该以默认设置的形式工作。
然而，把 ``send_charset`` 设置一下也是一个不错的主意::

  set send_charset="us-ascii:utf-8"

Mutt 是高度可配置的。 这里是个使用mutt通过 Gmail 发送的补丁的最小配置::

  # .muttrc
  # ================  IMAP  ====================
  set imap_user = 'yourusername@gmail.com'
  set imap_pass = 'yourpassword'
  set spoolfile = imaps://imap.gmail.com/INBOX
  set folder = imaps://imap.gmail.com/
  set record="imaps://imap.gmail.com/[Gmail]/Sent Mail"
  set postponed="imaps://imap.gmail.com/[Gmail]/Drafts"
  set mbox="imaps://imap.gmail.com/[Gmail]/All Mail"

  # ================  SMTP  ====================
  set smtp_url = "smtp://username@smtp.gmail.com:587/"
  set smtp_pass = $imap_pass
  set ssl_force_tls = yes # Require encrypted connection

  # ================  Composition  ====================
  set editor = `echo \$EDITOR`
  set edit_headers = yes  # See the headers when editing
  set charset = UTF-8     # value of $LANG; also fallback for send_charset
  # Sender, email address, and sign-off line must match
  unset use_domain        # because joe@localhost is just embarrassing
  set realname = "YOUR NAME"
  set from = "username@gmail.com"
  set use_from = yes

Mutt文档含有更多信息：

    https://gitlab.com/muttmua/mutt/-/wikis/UseCases/Gmail

    http://www.mutt.org/doc/manual/

Pine (TUI)
**********

Pine过去有一些空格删减问题，但是这些现在应该都被修复了。

如果可以，请使用alpine（pine的继承者）。

配置选项：

- 最近的版本需要 ``quell-flowed-text``
- ``no-strip-whitespace-before-send`` 选项也是需要的。


Sylpheed (GUI)
**************

- 内嵌文本可以很好的工作（或者使用附件）。
- 允许使用外部的编辑器。
- 收件箱较多时非常慢。
- 如果通过non-SSL连接，无法使用TLS SMTP授权。
- 撰写窗口的标尺很有用。
- 将地址添加到通讯簿时无法正确理解显示的名称。

Thunderbird (GUI)
*****************

Thunderbird是Outlook的克隆版本，它很容易损坏文本，但也有一些方法强制修正。

在完成修改后（包括安装扩展），您需要重新启动Thunderbird。

- 允许使用外部编辑器：

  使用Thunderbird发补丁最简单的方法是使用扩展来打开您最喜欢的外部编辑器。

  下面是一些能够做到这一点的扩展样例。

  - “External Editor Revived”

    https://github.com/Frederick888/external-editor-revived

    https://addons.thunderbird.net/en-GB/thunderbird/addon/external-editor-revived/

    它需要安装“本地消息主机（native messaging host）”。
    参见以下文档:
    https://github.com/Frederick888/external-editor-revived/wiki

  - “External Editor”

    https://github.com/exteditor/exteditor

    下载并安装此扩展，然后打开 :menuselection:`新建消息` 窗口, 用
    :menuselection:`查看-->工具栏-->自定义...` 给它增加一个按钮，直接点击此
    按钮即可使用外置编辑器。

    请注意，“External Editor”要求你的编辑器不能fork，换句话说，编辑器必须在
    关闭前不返回。你可能需要传递额外的参数或修改编辑器设置。最值得注意的是，
    如果您使用的是gvim，那么您必须将 :menuselection:`external editor` 设置的
    编辑器字段设置为 ``/usr/bin/gvim --nofork"`` （假设可执行文件在
    ``/usr/bin`` ），以传递 ``-f`` 参数。如果您正在使用其他编辑器，请阅读其
    手册了解如何处理。

若要修正内部编辑器，请执行以下操作：

- 修改你的Thunderbird设置，不要使用 ``format=flowed`` ！
  回到主窗口，按照
  :menuselection:`主菜单-->首选项-->常规-->配置编辑器...`
  打开Thunderbird的配置编辑器。

  - 将 ``mailnews.send_plaintext_flowed`` 设为 ``false``

  - 将 ``mailnews.wraplength`` 从 ``72`` 改为 ``0``

- 不要写HTML邮件！
  回到主窗口，打开
  :menuselection:`主菜单-->账户设置-->你的@邮件.地址-->通讯录/编写&地址簿` ，
  关掉 ``以HTML格式编写消息`` 。

- 只用纯文本格式查看邮件！
  回到主窗口， :menuselection:`主菜单-->查看-->消息体为-->纯文本` ！

TkRat (GUI)
***********

可以使用它。使用"Insert file..."或者外部的编辑器。

Gmail (Web GUI)
***************

不要使用它发送补丁。

Gmail网页客户端自动地把制表符转换为空格。

虽然制表符转换为空格问题可以被外部编辑器解决，但它同时还会使用回车换行把每行
拆分为78个字符。

另一个问题是Gmail还会把任何含有非ASCII的字符的消息改用base64编码，如欧洲人的
名字。

HacKerMaiL (TUI)
****************

HacKerMaiL (hkml) 是一个基于公共收件箱的简单邮件管理工具，它不需要订阅邮件列表。
该工具由 DAMON 维护者开发和维护，旨在支持 DAMON 和通用内核子系统的基本开发工作
流程。详细信息可参考 HacKerMaiL 的 README 文件
(https://github.com/sjp38/hackermail/blob/master/README.md)。

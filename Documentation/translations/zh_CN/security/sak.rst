.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/security/sak.rst

:翻译:

 张巍 zhangwei <zhangwei@cqsoftware.com.cn>

===========================
Linux 安全注意键（SAK）处理
===========================

:日期: 2001年3月18日
:作者: Andrew Morton

操作系统的安全注意键是一种安全工具，用于防止系统上存在特洛伊
木马密码捕获程序。它提供了一种无法规避的方式，用于终止所有可
能伪装成登录应用程序的程序。用户需要在登录系统之前输入这个安
全键。

从键盘输入的方式生成安全注意键，Linux提供了两种相似但不同的
方式。一种是按下ALT-SYSRQ-K组合键，但你不应该使用这种方式，
因为它只有在内核启用了SYSRQ支持的情况下才能使用。

正确生成SAK的方式是使用``loadkeys``来定义键序列。无论内核是否
编译了sysrq支持，这种方式都能够正常工作。

当键盘处于原始模式时，SAK 能够正常工作。这意味着，一旦定义，
SAK 将终止正在运行的 X 服务器。如果系统处于运行级别 5，X 服
务器将重新启动，这正是你希望发生的情况。

你应该使用什么键序列？ CTRL-ALT-DEL用于重启机器，CTRL-ALT-
BACKSPACE对X服务器有特殊作用。我们将选择CTRL-ALT-PAUSE。

在你的rc.sysinit（或rc.local）文件中，添加以下命令::

    echo "Control Alt keycode 101 = SAK" | /bin/loadkeys

就这样！只有超级用户才能重新编程SAK键。

.. note::

  1. Linux SAK据说并不是C2级安全性的系统所要求的"真正的SAK"。
     该原因作者也不知道

  2. 在键盘输入的模式下，SAK会终止所有打开了/dev/console的应用
     程序。

     但是不幸的是，这也包括一些你实际上不希望被终止的程序。原因是
     这些程序错误的保持了/dev/console的打开状态。务必确保向你的
     Linux发行版提供商投诉这个问题。

     你可以用以下的命令来识别将被SAK终止的程序::

        # ls -l /proc/[0-9]*/fd/* | grep console
        l-wx------    1 root     root           64 Mar 18 00:46 /proc/579/fd/0 -> /dev/console

     然后::

        # ps aux|grep 579
        root       579  0.0  0.1  1088  436 ?        S    00:43   0:00 gpm -t ps/2

     所以``gpm``会被SAK杀死。这应该gpm中的bug。它应该正在关闭标准输入，
     你可以通过查找initscript来启动gpm并更改它：

     老的::

        daemon gpm

     新的::

        daemon gpm < /dev/null

     Vixie cron似乎也有这个问题，并且需要采取相同的处理方式。

     此外，某个著名的Linux发行版在它的rc.sysinit和rc scripts的脚本中
     包含了以下三行代码::

        exec 3<&0
        exec 4>&1
        exec 5>&2

     这些代码会导致所有的守护进程将文件描述符3、4和5关联到/dev/console。
     所以SAK会将他们所有都终止。一个简单的解决办法就是删掉这些代码，但是
     这样做会导致系统管理应用程序出现异常 - 要对所有的情况进行充分测试。

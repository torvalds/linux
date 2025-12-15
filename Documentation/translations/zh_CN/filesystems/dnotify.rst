.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/filesystems/dnotify.rst

:翻译:

   王龙杰 Wang Longjie <wang.longjie1@zte.com.cn>

==============
Linux 目录通知
==============

	   Stephen Rothwell <sfr@canb.auug.org.au>

目录通知的目的是使用户应用程序能够在目录或目录中的任何文件发生变更时收到通知。基本机制包括应用程序
通过 fcntl(2) 调用在目录上注册通知，通知本身则通过信号传递。

应用程序可以决定希望收到哪些 “事件” 的通知。当前已定义的事件如下：

	=========	=====================================
	DN_ACCESS	目录中的文件被访问（read）
	DN_MODIFY	目录中的文件被修改（write,truncate）
	DN_CREATE	目录中创建了文件
	DN_DELETE	目录中的文件被取消链接
	DN_RENAME	目录中的文件被重命名
	DN_ATTRIB	目录中的文件属性被更改（chmod,chown）
	=========	=====================================

通常，应用程序必须在每次通知后重新注册，但如果将 DN_MULTISHOT 与事件掩码进行或运算，则注册
将一直保持有效，直到被显式移除（通过注册为不接收任何事件）。

默认情况下，SIGIO 信号将被传递给进程，且不附带其他有用的信息。但是，如果使用 F_SETSIG fcntl(2)
调用让内核知道要传递哪个信号，一个 siginfo 结构体将被传递给信号处理程序，该结构体的 si_fd 成员将
包含与发生事件的目录相关联的文件描述符。

应用程序最好选择一个实时信号（SIGRTMIN + <n>），以便通知可以被排队。如果指定了 DN_MULTISHOT，
这一点尤为重要。注意，SIGRTMIN 通常是被阻塞的，因此最好使用（至少）SIGRTMIN + 1。

实现预期（特性与缺陷 :-)）
--------------------------

对于文件的任何本地访问，通知都应能正常工作，即使实际文件系统位于远程服务器上。这意味着，对本地用户
模式服务器提供的文件的远程访问应能触发通知。同样的，对本地内核 NFS 服务器提供的文件的远程访问
也应能触发通知。

为了尽可能减小对文件系统代码的影响，文件硬链接的问题已被忽略。因此，如果一个文件（x）存在于两个
目录（a 和 b）中，通过名称”a/x”对该文件进行的更改应通知给期望接收目录“a”通知的程序，但不会
通知给期望接收目录“b”通知的程序。

此外，取消链接的文件仍会在它们链接到的最后一个目录中触发通知。

配置
----

Dnotify 由 CONFIG_DNOTIFY 配置选项控制。禁用该选项时，fcntl(fd, F_NOTIFY, ...) 将返
回 -EINVAL。

示例
----
具体示例可参见 tools/testing/selftests/filesystems/dnotify_test.c。

注意
----
从 Linux 2.6.13 开始，dnotify 已被 inotify 取代。有关 inotify 的更多信息，请参见
Documentation/filesystems/inotify.rst。

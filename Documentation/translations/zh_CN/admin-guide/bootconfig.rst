.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/admin-guide/bootconfig.rst

:译者: 吴想成 Wu XiangCheng <bobwxc@email.cn>

========
引导配置
========

:作者: Masami Hiramatsu <mhiramat@kernel.org>

概述
====

引导配置扩展了现有的内核命令行，以一种更有效率的方式在引导内核时进一步支持
键值数据。这允许管理员传递一份结构化关键字的配置文件。

配置文件语法
============

引导配置文件的语法采用非常简单的键值结构。每个关键字由点连接的单词组成，键
和值由 ``=`` 连接。值以分号（ ``;`` ）或换行符（ ``\n`` ）结尾。数组值中每
个元素由逗号（ ``,`` ）分隔。::

  KEY[.WORD[...]] = VALUE[, VALUE2[...]][;]

与内核命令行语法不同，逗号和 ``=`` 周围允许有空格。

关键字只允许包含字母、数字、连字符（ ``-`` ）和下划线（ ``_`` ）。值可包含
可打印字符和空格，但分号（ ``;`` ）、换行符（ ``\n`` ）、逗号（ ``,`` ）、
井号（ ``#`` ）和右大括号（ ``}`` ）等分隔符除外。

如果你需要在值中使用这些分隔符，可以用双引号（ ``"VALUE"`` ）或单引号
（ ``'VALUE'`` ）括起来。注意，引号无法转义。

键的值可以为空或不存在。这些键用于检查该键是否存在（类似布尔值）。

键值语法
--------

引导配置文件语法允许用户通过大括号合并键名部分相同的关键字。例如::

 foo.bar.baz = value1
 foo.bar.qux.quux = value2

也可以写成::

 foo.bar {
    baz = value1
    qux.quux = value2
 }

或者更紧凑一些，写成::

 foo.bar { baz = value1; qux.quux = value2 }

在这两种样式中，引导解析时相同的关键字都会自动合并。因此可以追加类似的树或
键值。

相同关键字的值
--------------

禁止两个或多个值或数组共享同一个关键字。例如::

 foo = bar, baz
 foo = qux  # !错误! 我们不可以重定义相同的关键字

如果你想要更新值，必须显式使用覆盖操作符 ``:=`` 。例如::

 foo = bar, baz
 foo := qux

这样 ``foo`` 关键字的值就变成了 ``qux`` 。这对于通过添加（部分）自定义引导
配置来覆盖默认值非常有用，免于解析默认引导配置。

如果你想对现有关键字追加值作为数组成员，可以使用 ``+=`` 操作符。例如::

 foo = bar, baz
 foo += qux

这样， ``foo`` 关键字就同时拥有了 ``bar`` ， ``baz`` 和 ``qux`` 。

此外，父关键字下可同时存在值和子关键字。
例如，下列配置是可行的。::

 foo = value1
 foo.bar = value2
 foo := value3 # 这会更新foo的值。

注意，裸值不能直接放进结构化关键字中，必须在大括号外定义它。例如::

 foo {
     bar = value1
     bar {
         baz = value2
         qux = value3
     }
 }

同时，关键字下值节点的顺序是固定的。如果值和子关键字同时存在，值永远是该关
键字的第一个子节点。因此如果用户先指定子关键字，如::

 foo.bar = value1
 foo = value2

则在程序（和/proc/bootconfig）中，它会按如下显示::

 foo = value2
 foo.bar = value1

注释
----

配置语法接受shell脚本风格的注释。注释以井号（ ``#`` ）开始，到换行符
（ ``\n`` ）结束。

::

 # comment line
 foo = value # value is set to foo.
 bar = 1, # 1st element
       2, # 2nd element
       3  # 3rd element

会被解析为::

 foo = value
 bar = 1, 2, 3

注意你不能把注释放在值和分隔符（ ``,`` 或 ``;`` ）之间。如下配置语法是错误的::

 key = 1 # comment
       ,2


/proc/bootconfig
================

/proc/bootconfig是引导配置的用户空间接口。与/proc/cmdline不同，此文件内容以
键值列表样式显示。
每个键值对一行，样式如下::

 KEY[.WORDS...] = "[VALUE]"[,"VALUE2"...]


用引导配置引导内核
==================

用引导配置引导内核有两种方法：将引导配置附加到initrd镜像或直接嵌入内核中。

*initrd: initial RAM disk，初始内存磁盘*

将引导配置附加到initrd
----------------------

由于默认情况下引导配置文件是用initrd加载的，因此它将被添加到initrd（initramfs）
镜像文件的末尾，其中包含填充、大小、校验值和12字节幻数，如下所示::

 [initrd][bootconfig][padding][size(le32)][checksum(le32)][#BOOTCONFIG\n]

大小和校验值为小端序存放的32位无符号值。

当引导配置被加到initrd镜像时，整个文件大小会对齐到4字节。空字符（ ``\0`` ）
会填补对齐空隙。因此 ``size`` 就是引导配置文件的长度+填充的字节。

Linux内核在内存中解码initrd镜像的最后部分以获取引导配置数据。由于这种“背负式”
的方法，只要引导加载器传递了正确的initrd文件大小，就无需更改或更新引导加载器
和内核镜像本身。如果引导加载器意外传递了更长的大小，内核将无法找到引导配置数
据。

Linux内核在tools/bootconfig下提供了 ``bootconfig`` 命令来完成此操作，管理员
可以用它从initrd镜像中删除或追加配置文件。你可以用以下命令来构建它::

 # make -C tools/bootconfig

要向initrd镜像添加你的引导配置文件，请按如下命令操作（旧数据会自动移除）::

 # tools/bootconfig/bootconfig -a your-config /boot/initrd.img-X.Y.Z

要从镜像中移除配置，可以使用-d选项::

 # tools/bootconfig/bootconfig -d /boot/initrd.img-X.Y.Z

然后在内核命令行上添加 ``bootconfig`` 告诉内核去initrd文件末尾寻找内核配置。

将引导配置嵌入内核
------------------

如果你不能使用initrd，也可以通过Kconfig选项将引导配置文件嵌入内核中。在此情
况下，你需要用以下选项重新编译内核::

 CONFIG_BOOT_CONFIG_EMBED=y
 CONFIG_BOOT_CONFIG_EMBED_FILE="/引导配置/文件/的/路径"

``CONFIG_BOOT_CONFIG_EMBED_FILE`` 需要从源码树或对象树开始的引导配置文件的
绝对/相对路径。内核会将其嵌入作为默认引导配置。

与将引导配置附加到initrd一样，你也需要在内核命令行上添加 ``bootconfig`` 告诉
内核去启用内嵌的引导配置。

注意，即使你已经设置了此选项，仍可用附加到initrd的其他引导配置覆盖内嵌的引导
配置。

通过引导配置传递内核参数
========================

除了内核命令行，引导配置也可以用于传递内核参数。所有 ``kernel`` 关键字下的键
值对都将直接传递给内核命令行。此外， ``init`` 下的键值对将通过命令行传递给
init进程。参数按以下顺序与用户给定的内核命令行字符串相连，因此命令行参数可以
覆盖引导配置参数（这取决于子系统如何处理参数，但通常前面的参数将被后面的参数
覆盖）::

 [bootconfig params][cmdline params] -- [bootconfig init params][cmdline init params]

如果引导配置文件给出的kernel/init参数是::

 kernel {
   root = 01234567-89ab-cdef-0123-456789abcd
 }
 init {
  splash
 }

这将被复制到内核命令行字符串中，如下所示::

 root="01234567-89ab-cdef-0123-456789abcd" -- splash

如果用户给出的其他命令行是::

 ro bootconfig -- quiet

则最后的内核命令行如下::

 root="01234567-89ab-cdef-0123-456789abcd" ro bootconfig -- splash quiet


配置文件的限制
==============

当前最大的配置大小是32KB，关键字总数（不是键值条目）必须少于1024个节点。
注意：这不是条目数而是节点数，条目必须消耗超过2个节点（一个关键字和一个值）。
所以从理论上讲最多512个键值对。如果关键字平均包含3个单词，则可有256个键值对。
在大多数情况下，配置项的数量将少于100个条目，小于8KB，因此这应该足够了。如果
节点数超过1024，解析器将返回错误，即使文件大小小于32KB。（请注意，此最大尺寸
不包括填充的空字符。）
无论如何，因为 ``bootconfig`` 命令在附加启动配置到initrd映像时会验证它，用户
可以在引导之前注意到它。


引导配置API
===========

用户可以查询或遍历键值对，也可以查找（前缀）根关键字节点，并在查找该节点下的
键值。

如果您有一个关键字字符串，则可以直接使用 xbc_find_value() 查询该键的值。如果
你想知道引导配置里有哪些关键字，可以使用 xbc_for_each_key_value() 迭代键值对。
请注意，您需要使用 xbc_array_for_each_value() 访问数组的值，例如::

 vnode = NULL;
 xbc_find_value("key.word", &vnode);
 if (vnode && xbc_node_is_array(vnode))
    xbc_array_for_each_value(vnode, value) {
      printk("%s ", value);
    }

如果您想查找具有前缀字符串的键，可以使用 xbc_find_node() 通过前缀字符串查找
节点，然后用 xbc_node_for_each_key_value() 迭代前缀节点下的键。

但最典型的用法是获取前缀下的命名值或前缀下的命名数组，例如::

 root = xbc_find_node("key.prefix");
 value = xbc_node_find_value(root, "option", &vnode);
 ...
 xbc_node_for_each_array_value(root, "array-option", value, anode) {
    ...
 }

这将访问值“key.prefix.option”的值和“key.prefix.array-option”的数组。

锁是不需要的，因为在初始化之后配置只读。如果需要修改，必须复制所有数据和关键字。


函数与结构体
============

相关定义的kernel-doc参见：

 - include/linux/bootconfig.h
 - lib/bootconfig.c

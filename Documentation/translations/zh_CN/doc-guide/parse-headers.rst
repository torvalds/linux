.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/doc-guide/parse-headers.rst

:译者: 吴想成 Wu XiangCheng <bobwxc@email.cn>

=====================
包含用户空间API头文件
=====================

有时，为了描述用户空间API并在代码和文档之间生成交叉引用，需要包含头文件和示例
C代码。为用户空间API文件添加交叉引用还有一个好处：如果在文档中找不到相应符号，
Sphinx将生成警告。这有助于保持用户空间API文档与内核更改同步。
:ref:`parse_headers.pl <parse_headers_zh>` 提供了生成此类交叉引用的一种方法。
在构建文档时，必须通过Makefile调用它。有关如何在内核树中使用它的示例，请参阅
``Documentation/userspace-api/media/Makefile`` 。

.. _parse_headers_zh:

parse_headers.pl
----------------

脚本名称
~~~~~~~~


parse_headers.pl——解析一个C文件，识别函数、结构体、枚举、定义并对Sphinx文档
创建交叉引用。


用法概要
~~~~~~~~


\ **parse_headers.pl**\  [<选项>] <C文件> <输出文件> [<例外文件>]

<选项> 可以是： --debug, --help 或 --usage 。


选项
~~~~



\ **--debug**\

 开启脚本详细模式，在调试时很有用。


\ **--usage**\

 打印简短的帮助信息并退出。



\ **--help**\

 打印更详细的帮助信息并退出。


说明
~~~~

通过C头文件或源文件（<C文件>）中为描述API的文档编写的带交叉引用的 ..预格式化
文本 块将文件转换成重构文本（RST）。它接受一个可选的<例外文件>，其中描述了
哪些元素将被忽略或指向非默认引用。

输出被写入到<输出文件>。

它能够识别定义、函数、结构体、typedef、枚举和枚举符号，并为它们创建交叉引用。
它还能够区分用于指定Linux ioctl的 ``#define`` 。

<例外文件> 包含两种类型的语句： \ **ignore**\  或 \ **replace**\ .

ignore标记的语法为：


ignore \ **type**\  \ **name**\

The \ **ignore**\  意味着它不会为类型为 \ **type**\ 的 \ **name**\ 符号生成
交叉引用。


replace标记的语法为：


replace \ **type**\  \ **name**\  \ **new_value**\

The \ **replace**\  味着它将为 \ **type**\ 类型的 \ **name**\ 符号生成交叉引
用，但是它将使用 \ **new_value**\ 来取代默认的替换规则。


这两种语句中， \ **type**\ 可以是以下任一项：


\ **ioctl**\

 ignore 或 replace 语句应用于ioctl定义，如：

 #define	VIDIOC_DBG_S_REGISTER 	 _IOW('V', 79, struct v4l2_dbg_register)



\ **define**\

 ignore 或 replace 语句应用于在<C文件>中找到的任何其他 ``#define`` 。



\ **typedef**\

 ignore 和 replace 语句应用于<C文件>中的typedef语句。



\ **struct**\

 ignore 和 replace 语句应用于<C文件>中的结构体名称语句。



\ **enum**\

 ignore 和 replace 语句应用于<C文件>中的枚举名称语句。



\ **symbol**\

 ignore 和 replace 语句应用于<C文件>中的枚举值名称语句。

 replace语句中， \ **new_value**\  会自动使用 \ **typedef**\ , \ **enum**\
 和 \ **struct**\ 类型的 :c:type: 引用；以及 \ **ioctl**\ , \ **define**\  和
 \ **symbol**\ 类型的  :ref: 。引用的类型也可以在replace语句中显式定义。


示例
~~~~


ignore define _VIDEODEV2_H


忽略<C文件>中的 #define _VIDEODEV2_H 。

ignore symbol PRIVATE


如下结构体：

enum foo { BAR1, BAR2, PRIVATE };

不会为 \ **PRIVATE**\ 生成交叉引用。

replace symbol BAR1 :c:type:\`foo\`
replace symbol BAR2 :c:type:\`foo\`


如下结构体：

enum foo { BAR1, BAR2, PRIVATE };

它会让BAR1和BAR2枚举符号交叉引用C域中的foo符号。



缺陷
~~~~


请向Mauro Carvalho Chehab <mchehab@kernel.org>报告有关缺陷。

中文翻译问题请找中文翻译维护者。


版权
~~~~


版权所有 (c) 2016 Mauro Carvalho Chehab <mchehab+samsung@kernel.org>

许可证 GPLv2：GNU GPL version 2 <https://gnu.org/licenses/gpl.html>

这是自由软件：你可以自由地修改和重新发布它。
在法律允许的范围内，**不提供任何保证**。

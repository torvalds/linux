.. SPDX-License-Identifier: GPL-2.0

.. _kernel_licensing:

Linux内核许可规则
=================

Linux内核根据LICENSES/preferred/GPL-2.0中提供的GNU通用公共许可证版本2
（GPL-2.0）的条款提供，并在LICENSES/exceptions/Linux-syscall-note中显式
描述了例外的系统调用，如COPYING文件中所述。

此文档文件提供了如何对每个源文件进行注释以使其许可证清晰明确的说明。
它不会取代内核的许可证。

内核源代码作为一个整体适用于COPYING文件中描述的许可证，但是单个源文件可以
具有不同的与GPL-20兼容的许可证::

    GPL-1.0+ : GNU通用公共许可证v1.0或更高版本
    GPL-2.0+ : GNU通用公共许可证v2.0或更高版本
    LGPL-2.0 : 仅限GNU库通用公共许可证v2
    LGPL-2.0+: GNU 库通用公共许可证v2或更高版本
    LGPL-2.1 : 仅限GNU宽通用公共许可证v2.1
    LGPL-2.1+: GNU宽通用公共许可证v2.1或更高版本

除此之外，个人文件可以在双重许可下提供，例如一个兼容的GPL变体，或者BSD，
MIT等许可。

用户空间API（UAPI）头文件描述了用户空间程序与内核的接口，这是一种特殊情况。
根据内核COPYING文件中的注释，syscall接口是一个明确的边界，它不会将GPL要求
扩展到任何使用它与内核通信的软件。由于UAPI头文件必须包含在创建在Linux内核
上运行的可执行文件的任何源文件中，因此此例外必须记录在特别的许可证表述中。

表达源文件许可证的常用方法是将匹配的样板文本添加到文件的顶部注释中。由于
格式，拼写错误等，这些“样板”很难通过那些在上下文中使用的验证许可证合规性
的工具。

样板文本的替代方法是在每个源文件中使用软件包数据交换（SPDX）许可证标识符。
SPDX许可证标识符是机器可解析的，并且是用于提供文件内容的许可证的精确缩写。
SPDX许可证标识符由Linux 基金会的SPDX 工作组管理，并得到了整个行业，工具
供应商和法律团队的合作伙伴的一致同意。有关详细信息，请参阅
https://spdx.org/

Linux内核需要所有源文件中的精确SPDX标识符。内核中使用的有效标识符在
`许可标识符`_ 一节中进行了解释，并且已可以在
https://spdx.org/licenses/ 上的官方SPDX许可证列表中检索，并附带许可证
文本。

许可标识符语法
--------------

1.安置:

   内核文件中的SPDX许可证标识符应添加到可包含注释的文件中的第一行。对于大多
   数文件，这是第一行，除了那些在第一行中需要'#!PATH_TO_INTERPRETER'的脚本。
   对于这些脚本，SPDX标识符进入第二行。

|

2. 风格:

   SPDX许可证标识符以注释的形式添加。注释样式取决于文件类型::

      C source:	// SPDX-License-Identifier: <SPDX License Expression>
      C header:	/* SPDX-License-Identifier: <SPDX License Expression> */
      ASM:	/* SPDX-License-Identifier: <SPDX License Expression> */
      scripts:	# SPDX-License-Identifier: <SPDX License Expression>
      .rst:	.. SPDX-License-Identifier: <SPDX License Expression>
      .dts{i}:	// SPDX-License-Identifier: <SPDX License Expression>

   如果特定工具无法处理标准注释样式，则应使用工具接受的相应注释机制。这是在
   C 头文件中使用“/\*\*/”样式注释的原因。过去在使用生成的.lds文件中观察到
   构建被破坏，其中'ld'无法解析C++注释。现在已经解决了这个问题，但仍然有较
   旧的汇编程序工具无法处理C++样式的注释。

|

3. 句法:

   <SPDX许可证表达式>是SPDX许可证列表中的SPDX短格式许可证标识符，或者在许可
   证例外适用时由“WITH”分隔的两个SPDX短格式许可证标识符的组合。当应用多个许
   可证时，表达式由分隔子表达式的关键字“AND”，“OR”组成，并由“（”，“）”包围。

   带有“或更高”选项的[L]GPL等许可证的许可证标识符通过使用“+”来表示“或更高”
   选项来构建。::

      // SPDX-License-Identifier: GPL-2.0+
      // SPDX-License-Identifier: LGPL-2.1+

   当需要修正的许可证时，应使用WITH。 例如，linux内核UAPI文件使用表达式::

      // SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
      // SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note

   其它在内核中使用WITH例外的事例如下::

      // SPDX-License-Identifier: GPL-2.0 WITH mif-exception
      // SPDX-License-Identifier: GPL-2.0+ WITH GCC-exception-2.0

   例外只能与特定的许可证标识符一起使用。有效的许可证标识符列在异常文本文件
   的标记中。有关详细信息，请参阅“许可证标识符”_一章中的“例外”

   如果文件是双重许可且只选择一个许可证，则应使用OR。例如，一些dtsi文件在双
   许可下可用::

      // SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

   内核中双许可文件中许可表达式的示例::

      // SPDX-License-Identifier: GPL-2.0 OR MIT
      // SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
      // SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
      // SPDX-License-Identifier: GPL-2.0 OR MPL-1.1
      // SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) OR MIT
      // SPDX-License-Identifier: GPL-1.0+ OR BSD-3-Clause OR OpenSSL

   如果文件具有多个许可证，其条款全部适用于使用该文件，则应使用AND。例如，
   如果代码是从另一个项目继承的，并且已经授予了将其放入内核的权限，但原始
   许可条款需要保持有效::

      // SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) AND MIT

   另一个需要遵守两套许可条款的例子是::

      // SPDX-License-Identifier: GPL-1.0+ AND LGPL-2.1+

许可标识符
----------

当前使用的许可证以及添加到内核的代码许可证可以分解为：

1. _`优先许可`:

   应尽可能使用这些许可证，因为它们已知完全兼容并广泛使用。这些许可证在内核
   目录::

      LICENSES/preferred/

   此目录中的文件包含完整的许可证文本和`元标记`_。文件名与SPDX许可证标识
   符相同，后者应用于源文件中的许可证。

   例如::

      LICENSES/preferred/GPL-2.0

   包含GPLv2许可证文本和所需的元标签::

      LICENSES/preferred/MIT

   包含MIT许可证文本和所需的元标记

   _`元标记`:

   许可证文件中必须包含以下元标记：

   - Valid-License-Identifier:

     一行或多行, 声明那些许可标识符在项目内有效, 以引用此特定许可的文本。通
     常这是一个有效的标识符，但是例如对于带有'或更高'选项的许可证，两个标识
     符都有效。

   - SPDX-URL:

     SPDX页面的URL，其中包含与许可证相关的其他信息.

   - Usage-Guidance:

     使用建议的自由格式文本。该文本必须包含SPDX许可证标识符的正确示例，因为
     它们应根据`许可标识符语法`_ 指南放入源文件中。

   - License-Text:

     此标记之后的所有文本都被视为原始许可文本

   文件格式示例::

      Valid-License-Identifier: GPL-2.0
      Valid-License-Identifier: GPL-2.0+
      SPDX-URL: https://spdx.org/licenses/GPL-2.0.html
      Usage-Guide:
        To use this license in source code, put one of the following SPDX
	tag/value pairs into a comment according to the placement
	guidelines in the licensing rules documentation.
	For 'GNU General Public License (GPL) version 2 only' use:
	  SPDX-License-Identifier: GPL-2.0
	For 'GNU General Public License (GPL) version 2 or any later version' use:
	  SPDX-License-Identifier: GPL-2.0+
      License-Text:
        Full license text

   ::

      SPDX-License-Identifier: MIT
      SPDX-URL: https://spdx.org/licenses/MIT.html
      Usage-Guide:
	To use this license in source code, put the following SPDX
	tag/value pair into a comment according to the placement
	guidelines in the licensing rules documentation.
	  SPDX-License-Identifier: MIT
      License-Text:
        Full license text

|

2. 不推荐的许可证:

   这些许可证只应用于现有代码或从其他项目导入代码。这些许可证在内核目录::

      LICENSES/other/

   此目录中的文件包含完整的许可证文本和`元标记`_。文件名与SPDX许可证标识
   符相同，后者应用于源文件中的许可证。

   例如::

      LICENSES/other/ISC

   包含国际系统联合许可文本和所需的元标签::

      LICENSES/other/ZLib

   包含ZLIB许可文本和所需的元标签.

   元标签:

   “其他”许可证的元标签要求与`优先许可`_的要求相同。

   文件格式示例::

      Valid-License-Identifier: ISC
      SPDX-URL: https://spdx.org/licenses/ISC.html
      Usage-Guide:
        Usage of this license in the kernel for new code is discouraged
	and it should solely be used for importing code from an already
	existing project.
        To use this license in source code, put the following SPDX
	tag/value pair into a comment according to the placement
	guidelines in the licensing rules documentation.
	  SPDX-License-Identifier: ISC
      License-Text:
        Full license text

|

3. _`例外`:

   某些许可证可以修改，并允许原始许可证不具有的某些例外权利。这些例外在
   内核目录::

      LICENSES/exceptions/

   此目录中的文件包含完整的例外文本和所需的`例外元标记`_.

   例如::

      LICENSES/exceptions/Linux-syscall-note

   包含Linux内核的COPYING文件中记录的Linux系统调用例外，该文件用于UAPI
   头文件。例如::

      LICENSES/exceptions/GCC-exception-2.0

   包含GCC'链接例外'，它允许独立于其许可证的任何二进制文件与标记有此例外的
   文件的编译版本链接。这是从GPL不兼容源代码创建可运行的可执行文件所必需的。

   _`例外元标记`:

   以下元标记必须在例外文件中可用：

   - SPDX-Exception-Identifier:

     一个可与SPDX许可证标识符一起使用的例外标识符。

   - SPDX-URL:

     SPDX页面的URL，其中包含与例外相关的其他信息。

   - SPDX-Licenses:

     以逗号分隔的例外可用的SPDX许可证标识符列表。

   - Usage-Guidance:

     使用建议的自由格式文本。必须在文本后面加上SPDX许可证标识符的正确示例，
     因为它们应根据`License identifier syntax`_指南放入源文件中。

   - Exception-Text:

     此标记之后的所有文本都被视为原始异常文本

   文件格式示例::

      SPDX-Exception-Identifier: Linux-syscall-note
      SPDX-URL: https://spdx.org/licenses/Linux-syscall-note.html
      SPDX-Licenses: GPL-2.0, GPL-2.0+, GPL-1.0+, LGPL-2.0, LGPL-2.0+, LGPL-2.1, LGPL-2.1+
      Usage-Guidance:
        This exception is used together with one of the above SPDX-Licenses
	to mark user-space API (uapi) header files so they can be included
	into non GPL compliant user-space application code.
        To use this exception add it with the keyword WITH to one of the
	identifiers in the SPDX-Licenses tag:
	  SPDX-License-Identifier: <SPDX-License> WITH Linux-syscall-note
      Exception-Text:
        Full exception text

   ::

      SPDX-Exception-Identifier: GCC-exception-2.0
      SPDX-URL: https://spdx.org/licenses/GCC-exception-2.0.html
      SPDX-Licenses: GPL-2.0, GPL-2.0+
      Usage-Guidance:
        The "GCC Runtime Library exception 2.0" is used together with one
	of the above SPDX-Licenses for code imported from the GCC runtime
	library.
        To use this exception add it with the keyword WITH to one of the
	identifiers in the SPDX-Licenses tag:
	  SPDX-License-Identifier: <SPDX-License> WITH GCC-exception-2.0
      Exception-Text:
        Full exception text


所有SPDX许可证标识符和例外都必须在LICENSES子目录中具有相应的文件。这是允许
工具验证（例如checkpatch.pl）以及准备好从源读取和提取许可证所必需的, 这是
各种FOSS组织推荐的，例如 `FSFE REUSE initiative <https://reuse.software/>`_.

_`模块许可`
-----------------

   可加载内核模块还需要MODULE_LICENSE（）标记。此标记既不替代正确的源代码
   许可证信息（SPDX-License-Identifier），也不以任何方式表示或确定提供模块
   源代码的确切许可证。

   此标记的唯一目的是提供足够的信息，该模块是否是自由软件或者是内核模块加
   载器和用户空间工具的专有模块。

   MODULE_LICENSE（）的有效许可证字符串是:

    ============================= =============================================
    "GPL"			  模块是根据GPL版本2许可的。这并不表示仅限于
                                  GPL-2.0或GPL-2.0或更高版本之间的任何区别。
                                  最正确许可证信息只能通过相应源文件中的许可证
                                  信息来确定

    "GPL v2"			  和"GPL"相同，它的存在是因为历史原因。

    "GPL and additional rights"   表示模块源在GPL v2变体和MIT许可下双重许可的
                                  历史变体。请不要在新代码中使用。

    "Dual MIT/GPL"		  表达该模块在GPL v2变体或MIT许可证选择下双重
                                  许可的正确方式。

    "Dual BSD/GPL"		  该模块根据GPL v2变体或BSD许可证选择进行双重
                                  许可。 BSD许可证的确切变体只能通过相应源文件
                                  中的许可证信息来确定。

    "Dual MPL/GPL"		  该模块根据GPL v2变体或Mozilla Public License
                                  （MPL）选项进行双重许可。 MPL许可证的确切变体
                                  只能通过相应的源文件中的许可证信息来确定。

    "Proprietary"		  该模块属于专有许可。此字符串仅用于专有的第三
                                  方模块，不能用于在内核树中具有源代码的模块。
                                  以这种方式标记的模块在加载时会使用'P'标记污
                                  染内核，并且内核模块加载器拒绝将这些模块链接
                                  到使用EXPORT_SYMBOL_GPL（）导出的符号。
    ============================= =============================================


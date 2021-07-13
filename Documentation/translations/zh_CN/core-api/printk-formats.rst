.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/printk-formats.rst
:Translator: Yanteng Si <siyanteng@loongson.cn>

.. _cn_printk-formats.rst:


==============================
如何获得正确的printk格式占位符
==============================



:作者: Randy Dunlap <rdunlap@infradead.org>
:作者: Andrew Murray <amurray@mpc-data.co.uk>


整数类型
========

::

	        若变量类型是Type，则使用printk格式占位符。
	        -------------------------------------------
		char			%d 或 %x
		unsigned char		%u 或 %x
		short int		%d 或 %x
		unsigned short int	%u 或 %x
		int			%d 或 %x
		unsigned int		%u 或 %x
		long			%ld 或 %lx
		unsigned long		%lu 或 %lx
		long long		%lld 或 %llx
		unsigned long long	%llu 或 %llx
		size_t			%zu 或 %zx
		ssize_t			%zd 或 %zx
		s8			%d 或 %x
		u8			%u 或 %x
		s16			%d 或 %x
		u16			%u 或 %x
		s32			%d 或 %x
		u32			%u 或 %x
		s64			%lld 或 %llx
		u64			%llu 或 %llx


如果 <type> 的大小依赖于配置选项 (例如 sector_t, blkcnt_t) 或其大小依赖于架构
(例如 tcflag_t)，则使用其可能的最大类型的格式占位符并显式强制转换为它。

例如

::

	printk("test: sector number/total blocks: %llu/%llu\n",
		(unsigned long long)sector, (unsigned long long)blockcount);

提醒：sizeof()返回类型为size_t。

内核的printf不支持%n。显而易见，浮点格式(%e, %f, %g, %a)也不被识别。使用任何不
支持的占位符或长度限定符都会导致一个WARN并且终止vsnprintf()执行。

指针类型
========

一个原始指针值可以用%p打印，它将在打印前对地址进行哈希处理。内核也支持扩展占位符来打印
不同类型的指针。

一些扩展占位符会打印给定地址上的数据，而不是打印地址本身。在这种情况下，以下错误消息可能
会被打印出来，而不是无法访问的消息::

	(null)	 data on plain NULL address
	(efault) data on invalid address
	(einval) invalid data on a valid address

普通指针
----------

::

	%p	abcdef12 or 00000000abcdef12

没有指定扩展名的指针（即没有修饰符的%p）被哈希（hash），以防止内核内存布局消息的泄露。这
样还有一个额外的好处，就是提供一个唯一的标识符。在64位机器上，前32位被清零。当没有足够的
熵进行散列处理时，内核将打印(ptrval)代替

如果可能的话，使用专门的修饰符，如%pS或%pB（如下所述），以避免打印一个必须事后解释的非哈
希地址。如果不可能，而且打印地址的目的是为调试提供更多的消息，使用%p，并在调试过程中
用 ``no_hash_pointers`` 参数启动内核，这将打印所有未修改的%p地址。如果你 *真的* 想知
道未修改的地址，请看下面的%px。

如果（也只有在）你将地址作为虚拟文件的内容打印出来，例如在procfs或sysfs中（使用
seq_printf()，而不是printk()）由用户空间进程读取，使用下面描述的%pK修饰符，不
要用%p或%px。


错误指针
--------

::

	%pe	-ENOSPC

用于打印错误指针(即IS_ERR()为真的指针)的符号错误名。不知道符号名的错误值会以十进制打印，
而作为%pe参数传递的非ERR_PTR会被视为普通的%p。

符号/函数指针
-------------

::

	%pS	versatile_init+0x0/0x110
	%ps	versatile_init
	%pSR	versatile_init+0x9/0x110
		(with __builtin_extract_return_addr() translation)
	%pB	prev_fn_of_versatile_init+0x88/0x88


``S`` 和 ``s`` 占位符用于打印符号格式的指针。它们的结果是符号名称带有(S)或不带有(s)偏移
量。如果禁用KALLSYMS，则打印符号地址。

``B`` 占位符的结果是带有偏移量的符号名，在打印堆栈回溯时应该使用。占位符将考虑编译器优化
的影响，当使用尾部调用并使用noreturn GCC属性标记时，可能会发生这种优化。

如果指针在一个模块内，模块名称和可选的构建ID将被打印在符号名称之后，并在说明符的末尾添加
一个额外的 ``b`` 。

::

	%pS	versatile_init+0x0/0x110 [module_name]
	%pSb	versatile_init+0x0/0x110 [module_name ed5019fdf5e53be37cb1ba7899292d7e143b259e]
	%pSRb	versatile_init+0x9/0x110 [module_name ed5019fdf5e53be37cb1ba7899292d7e143b259e]
		(with __builtin_extract_return_addr() translation)
	%pBb	prev_fn_of_versatile_init+0x88/0x88 [module_name ed5019fdf5e53be37cb1ba7899292d7e143b259e]

来自BPF / tracing追踪的探查指针
----------------------------------

::

	%pks	kernel string
	%pus	user string

``k`` 和 ``u`` 指定符用于打印来自内核内存(k)或用户内存(u)的先前探测的内存。后面的 ``s`` 指
定符的结果是打印一个字符串。对于直接在常规的vsnprintf()中使用时，(k)和(u)注释被忽略，但是，当
在BPF的bpf_trace_printk()之外使用时，它会读取它所指向的内存，不会出现错误。

内核指针
--------

::

	%pK	01234567 or 0123456789abcdef

用于打印应该对非特权用户隐藏的内核指针。%pK的行为取决于kptr_restrict sysctl——详见
Documentation/admin-guide/sysctl/kernel.rst。

未经修改的地址
--------------

::

	%px	01234567 or 0123456789abcdef

对于打印指针，当你 *真的* 想打印地址时。在用%px打印指针之前，请考虑你是否泄露了内核内
存布局的敏感消息。%px在功能上等同于%lx（或%lu）。%px是首选，因为它在grep查找时更唯一。
如果将来我们需要修改内核处理打印指针的方式，我们将能更好地找到调用点。

在使用%px之前，请考虑使用%p并在调试过程中启用' ' no_hash_pointer ' '内核参数是否足
够(参见上面的%p描述)。%px的一个有效场景可能是在panic发生之前立即打印消息，这样无论如何
都可以防止任何敏感消息被利用，使用%px就不需要用no_hash_pointer来重现panic。

指针差异
--------

::

	%td	2560
	%tx	a00

为了打印指针的差异，使用ptrdiff_t的%t修饰符。

例如::

	printk("test: difference between pointers: %td\n", ptr2 - ptr1);

结构体资源（Resources）
-----------------------

::

	%pr	[mem 0x60000000-0x6fffffff flags 0x2200] or
		[mem 0x0000000060000000-0x000000006fffffff flags 0x2200]
	%pR	[mem 0x60000000-0x6fffffff pref] or
		[mem 0x0000000060000000-0x000000006fffffff pref]

用于打印结构体资源。 ``R`` 和 ``r`` 占位符的结果是打印出的资源带有（R）或不带有（r）解码标志
成员。

通过引用传递。

物理地址类型 phys_addr_t
------------------------

::

	%pa[p]	0x01234567 or 0x0123456789abcdef

用于打印phys_addr_t类型（以及它的衍生物，如resource_size_t），该类型可以根据构建选项而
变化，无论CPU数据真实物理地址宽度如何。

通过引用传递。

DMA地址类型dma_addr_t
---------------------

::

	%pad	0x01234567 or 0x0123456789abcdef

用于打印dma_addr_t类型，该类型可以根据构建选项而变化，而不考虑CPU数据路径的宽度。

通过引用传递。

原始缓冲区为转义字符串
----------------------

::

	%*pE[achnops]

用于将原始缓冲区打印成转义字符串。对于以下缓冲区::

		1b 62 20 5c 43 07 22 90 0d 5d

几个例子展示了如何进行转换（不包括两端的引号）。::

		%*pE		"\eb \C\a"\220\r]"
		%*pEhp		"\x1bb \C\x07"\x90\x0d]"
		%*pEa		"\e\142\040\\\103\a\042\220\r\135"

转换规则是根据可选的标志组合来应用的(详见:c:func:`string_escape_mem` 内核文档):

	- a - ESCAPE_ANY
	- c - ESCAPE_SPECIAL
	- h - ESCAPE_HEX
	- n - ESCAPE_NULL
	- o - ESCAPE_OCTAL
	- p - ESCAPE_NP
	- s - ESCAPE_SPACE

默认情况下，使用 ESCAPE_ANY_NP。

ESCAPE_ANY_NP是许多情况下的明智选择，特别是对于打印SSID。

如果字段宽度被省略，那么将只转义1个字节。

原始缓冲区为十六进制字符串
--------------------------

::

	%*ph	00 01 02  ...  3f
	%*phC	00:01:02: ... :3f
	%*phD	00-01-02- ... -3f
	%*phN	000102 ... 3f

对于打印小的缓冲区（最长64个字节），可以用一定的分隔符作为一个
十六进制字符串。对于较大的缓冲区，可以考虑使用
:c:func:`print_hex_dump` 。

MAC/FDDI地址
------------

::

	%pM	00:01:02:03:04:05
	%pMR	05:04:03:02:01:00
	%pMF	00-01-02-03-04-05
	%pm	000102030405
	%pmR	050403020100

用于打印以十六进制表示的6字节MAC/FDDI地址。 ``M`` 和 ``m`` 占位符导致打印的
地址有(M)或没有(m)字节分隔符。默认的字节分隔符是冒号（：）。

对于FDDI地址，可以在 ``M`` 占位符之后使用 ``F`` 说明，以使用破折号(——)分隔符
代替默认的分隔符。

对于蓝牙地址， ``R`` 占位符应使用在 ``M`` 占位符之后，以使用反转的字节顺序，适
合于以小尾端顺序的蓝牙地址的肉眼可见的解析。

通过引用传递。

IPv4地址
--------

::

	%pI4	1.2.3.4
	%pi4	001.002.003.004
	%p[Ii]4[hnbl]

用于打印IPv4点分隔的十进制地址。 ``I4`` 和 ``i4`` 占位符的结果是打印的地址
有(i4)或没有(I4)前导零。

附加的 ``h`` 、 ``n`` 、 ``b`` 和 ``l`` 占位符分别用于指定主机、网络、大
尾端或小尾端地址。如果没有提供占位符，则使用默认的网络/大尾端顺序。

通过引用传递。

IPv6 地址
---------

::

	%pI6	0001:0002:0003:0004:0005:0006:0007:0008
	%pi6	00010002000300040005000600070008
	%pI6c	1:2:3:4:5:6:7:8

用于打印IPv6网络顺序的16位十六进制地址。 ``I6`` 和 ``i6`` 占位符的结果是
打印的地址有(I6)或没有(i6)分号。始终使用前导零。

额外的 ``c`` 占位符可与 ``I`` 占位符一起使用，以打印压缩的IPv6地址，如
https://tools.ietf.org/html/rfc5952 所述

通过引用传递。

IPv4/IPv6地址(generic, with port, flowinfo, scope)
--------------------------------------------------

::

	%pIS	1.2.3.4		or 0001:0002:0003:0004:0005:0006:0007:0008
	%piS	001.002.003.004	or 00010002000300040005000600070008
	%pISc	1.2.3.4		or 1:2:3:4:5:6:7:8
	%pISpc	1.2.3.4:12345	or [1:2:3:4:5:6:7:8]:12345
	%p[Ii]S[pfschnbl]

用于打印一个IP地址，不需要区分它的类型是AF_INET还是AF_INET6。一个指向有效结构
体sockaddr的指针，通过 ``IS`` 或 ``IS`` 指定，可以传递给这个格式占位符。

附加的 ``p`` 、  ``f`` 和 ``s`` 占位符用于指定port(IPv4, IPv6)、
flowinfo (IPv6)和sope(IPv6)。port有一个 ``:`` 前缀，flowinfo是 ``/`` 和
范围是 ``%`` ，每个后面都跟着实际的值。

对于IPv6地址，如果指定了额外的指定符 ``c`` ，则使用
https://tools.ietf.org/html/rfc5952 描述的压缩IPv6地址。
如https://tools.ietf.org/html/draft-ietf-6man-text-addr-representation-07
所建议的，IPv6地址由'['，']'包围，以防止出现额外的占位符 ``p`` ， ``f`` 或 ``s`` 。

对于IPv4地址，也可以使用额外的 ``h`` ， ``n`` ， ``b`` 和 ``l`` 说
明符，但对于IPv6地址则忽略。

通过引用传递。

更多例子::

	%pISfc		1.2.3.4		or [1:2:3:4:5:6:7:8]/123456789
	%pISsc		1.2.3.4		or [1:2:3:4:5:6:7:8]%1234567890
	%pISpfc		1.2.3.4:12345	or [1:2:3:4:5:6:7:8]:12345/123456789

UUID/GUID地址
-------------

::

	%pUb	00010203-0405-0607-0809-0a0b0c0d0e0f
	%pUB	00010203-0405-0607-0809-0A0B0C0D0E0F
	%pUl	03020100-0504-0706-0809-0a0b0c0e0e0f
	%pUL	03020100-0504-0706-0809-0A0B0C0E0E0F

用于打印16字节的UUID/GUIDs地址。附加的 ``l`` , ``L`` , ``b`` 和 ``B`` 占位符用
于指定小写(l)或大写(L)十六进制表示法中的小尾端顺序，以及小写(b)或大写(B)十六进制表
示法中的大尾端顺序。

如果没有使用额外的占位符，则将打印带有小写十六进制表示法的默认大端顺序。

通过引用传递。

目录项（dentry）的名称
----------------------

::

	%pd{,2,3,4}
	%pD{,2,3,4}

用于打印dentry名称；如果我们用 :c:func:`d_move` 和它比较，名称可能是新旧混合的，但
不会oops。 %pd dentry比较安全，其相当于我们以前用的%s dentry->d_name.name，%pd<n>打
印 ``n`` 最后的组件。 %pD对结构文件做同样的事情。


通过引用传递。

块设备（block_device）名称
--------------------------

::

	%pg	sda, sda1 or loop0p1

用于打印block_device指针的名称。

va_format结构体
---------------

::

	%pV

用于打印结构体va_format。这些结构包含一个格式字符串
和va_list如下

::

	struct va_format {
		const char *fmt;
		va_list *va;
	};

实现 "递归vsnprintf"。

如果没有一些机制来验证格式字符串和va_list参数的正确性，请不要使用这个功能。

通过引用传递。

设备树节点
----------

::

	%pOF[fnpPcCF]


用于打印设备树节点结构。默认行为相当于%pOFf。

	- f - 设备节点全称
	- n - 设备节点名
	- p - 设备节点句柄
	- P - 设备节点路径规范(名称+@单位)
	- F - 设备节点标志
	- c - 主要兼容字符串
	- C - 全兼容字符串

当使用多个参数时，分隔符是':'。

例如

::

	%pOF	/foo/bar@0			- Node full name
	%pOFf	/foo/bar@0			- Same as above
	%pOFfp	/foo/bar@0:10			- Node full name + phandle
	%pOFfcF	/foo/bar@0:foo,device:--P-	- Node full name +
	                                          major compatible string +
						  node flags
							D - dynamic
							d - detached
							P - Populated
							B - Populated bus

通过引用传递。

Fwnode handles
--------------

::

	%pfw[fP]

用于打印fwnode_handles的消息。默认情况下是打印完整的节点名称，包括路径。
这些修饰符在功能上等同于上面的%pOF。

	- f - 节点的全名，包括路径。
	- P - 节点名称，包括地址（如果有的话）。

例如 (ACPI)

::

	%pfwf	\_SB.PCI0.CIO2.port@1.endpoint@0	- Full node name
	%pfwP	endpoint@0				- Node name

例如 (OF)

::

	%pfwf	/ocp@68000000/i2c@48072000/camera@10/port/endpoint - Full name
	%pfwP	endpoint				- Node name

时间和日期
----------

::

	%pt[RT]			YYYY-mm-ddTHH:MM:SS
	%pt[RT]s		YYYY-mm-dd HH:MM:SS
	%pt[RT]d		YYYY-mm-dd
	%pt[RT]t		HH:MM:SS
	%pt[RT][dt][r][s]

用于打印日期和时间::

	R  struct rtc_time structure
	T  time64_t type

以我们（人类）可读的格式。

默认情况下，年将以1900为单位递增，月将以1为单位递增。 使用%pt[RT]r (raw)
来抑制这种行为。

%pt[RT]s（空格）将覆盖ISO 8601的分隔符，在日期和时间之间使用''（空格）而
不是'T'（大写T）。当日期或时间被省略时，它不会有任何影响。

通过引用传递。

clk结构体
---------

::

	%pC	pll1
	%pCn	pll1

用于打印clk结构。%pC 和 %pCn 打印时钟的名称（通用时钟框架）或唯一的32位
ID（传统时钟框架）。

通过引用传递。

位图及其衍生物，如cpumask和nodemask
-----------------------------------

::

	%*pb	0779
	%*pbl	0,3-6,8-10

对于打印位图（bitmap）及其派生的cpumask和nodemask，%*pb输出以字段宽度为位数的位图，
%*pbl输出以字段宽度为位数的范围列表。

字段宽度用值传递，位图用引用传递。可以使用辅助宏cpumask_pr_args()和
nodemask_pr_args()来方便打印cpumask和nodemask。

标志位字段，如页标志、gfp_flags
-------------------------------

::

	%pGp	referenced|uptodate|lru|active|private|node=0|zone=2|lastcpupid=0x1fffff
	%pGg	GFP_USER|GFP_DMA32|GFP_NOWARN
	%pGv	read|exec|mayread|maywrite|mayexec|denywrite

将flags位字段打印为构造值的符号常量集合。标志的类型由第三个字符给出。目前支持的
是[p]age flags， [v]ma_flags(都期望 ``unsigned long *`` )和
[g]fp_flags(期望 ``gfp_t *`` )。标志名称和打印顺序取决于特定的类型。

注意，这种格式不应该直接用于跟踪点的:c:func:`TP_printk()` 部分。相反，应使
用 <trace/events/mmflags.h>中的show_*_flags()函数。

通过引用传递。

网络设备特性
------------

::

	%pNF	0x000000000000c000

用于打印netdev_features_t。

通过引用传递。

V4L2和DRM FourCC代码(像素格式)
------------------------------

::

	%p4cc

打印V4L2或DRM使用的FourCC代码，包括格式端序及其十六进制的数值。

通过引用传递。

例如::

	%p4cc	BG12 little-endian (0x32314742)
	%p4cc	Y10  little-endian (0x20303159)
	%p4cc	NV12 big-endian (0xb231564e)

谢谢
====

如果您添加了其他%p扩展，请在可行的情况下，用一个或多个测试用例扩展<lib/test_printf.c>。

谢谢你的合作和关注。

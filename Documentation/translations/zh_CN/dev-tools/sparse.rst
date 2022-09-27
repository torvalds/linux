Copyright 2004 Linus Torvalds
Copyright 2004 Pavel Machek <pavel@ucw.cz>
Copyright 2006 Bob Copeland <me@bobcopeland.com>

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/dev-tools/sparse.rst

:翻译:

 Li Yang <leoyang.li@nxp.com>

:校译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

.. _cn_sparse:

Sparse
======

Sparse是一个C程序的语义检查器；它可以用来发现内核代码的一些潜在问题。 关
于sparse的概述，请参见https://lwn.net/Articles/689907/；本文档包含
一些针对内核的sparse信息。
关于sparse的更多信息，主要是关于它的内部结构，可以在它的官方网页上找到：
https://sparse.docs.kernel.org。

使用 sparse 工具做类型检查
~~~~~~~~~~~~~~~~~~~~~~~~~~

"__bitwise" 是一种类型属性，所以你应该这样使用它::

        typedef int __bitwise pm_request_t;

        enum pm_request {
                PM_SUSPEND = (__force pm_request_t) 1,
                PM_RESUME = (__force pm_request_t) 2
        };

这样会使 PM_SUSPEND 和 PM_RESUME 成为位方式(bitwise)整数（使用"__force"
是因为 sparse 会抱怨改变位方式的类型转换，但是这里我们确实需要强制进行转
换）。而且因为所有枚举值都使用了相同的类型，这里的"enum pm_request"也将
会使用那个类型做为底层实现。

而且使用 gcc 编译的时候，所有的 __bitwise/__force 都会消失，最后在 gcc
看来它们只不过是普通的整数。

坦白来说，你并不需要使用枚举类型。上面那些实际都可以浓缩成一个特殊的"int
__bitwise"类型。

所以更简单的办法只要这样做::

	typedef int __bitwise pm_request_t;

	#define PM_SUSPEND ((__force pm_request_t) 1)
	#define PM_RESUME ((__force pm_request_t) 2)

现在你就有了严格的类型检查所需要的所有基础架构。

一个小提醒：常数整数"0"是特殊的。你可以直接把常数零当作位方式整数使用而
不用担心 sparse 会抱怨。这是因为"bitwise"（恰如其名）是用来确保不同位方
式类型不会被弄混（小尾模式，大尾模式，cpu尾模式，或者其他），对他们来说
常数"0"确实 **是** 特殊的。

使用sparse进行锁检查
--------------------

下面的宏对于 gcc 来说是未定义的，在 sparse 运行时定义，以使用sparse的“上下文”
跟踪功能，应用于锁定。 这些注释告诉 sparse 什么时候有锁，以及注释的函数的进入和
退出。

__must_hold - 指定的锁在函数进入和退出时被持有。

__acquires  - 指定的锁在函数退出时被持有，但在进入时不被持有。

__releases  - 指定的锁在函数进入时被持有，但在退出时不被持有。

如果函数在不持有锁的情况下进入和退出，在函数内部以平衡的方式获取和释放锁，则不
需要注释。
上面的三个注释是针对sparse否则会报告上下文不平衡的情况。

获取 sparse 工具
~~~~~~~~~~~~~~~~

你可以从 Sparse 的主页获取最新的发布版本：

	https://www.kernel.org/pub/software/devel/sparse/dist/

或者，你也可以使用 git 克隆最新的 sparse 开发版本：

	git://git.kernel.org/pub/scm/devel/sparse/sparse.git

一旦你下载了源码，只要以普通用户身份运行：

	make
	make install

如果是标准的用户，它将会被自动安装到你的~/bin目录下。

使用 sparse 工具
~~~~~~~~~~~~~~~~

用"make C=1"命令来编译内核，会对所有重新编译的 C 文件使用 sparse 工具。
或者使用"make C=2"命令，无论文件是否被重新编译都会对其使用 sparse 工具。
如果你已经编译了内核，用后一种方式可以很快地检查整个源码树。

make 的可选变量 CHECKFLAGS 可以用来向 sparse 工具传递参数。编译系统会自
动向 sparse 工具传递 -Wbitwise 参数。

注意sparse定义了__CHECKER__预处理器符号。
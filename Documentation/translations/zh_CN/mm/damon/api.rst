.. SPDX-License-Identifier: GPL-2.0

:Original: Documentation/mm/damon/api.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:


=======
API参考
=======

内核空间的程序可以使用下面的API来使用DAMON的每个功能。你所需要做的就是引用 ``damon.h`` ，
它位于源代码树的include/linux/。

结构体
======

该API在以下内核代码中:

include/linux/damon.h


函数
====

该API在以下内核代码中:

mm/damon/core.c

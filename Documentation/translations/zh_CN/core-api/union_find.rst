.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/union_find.rst

=============================
Linux中的并查集（Union-Find）
=============================


:日期: 2024年6月21日
:作者: Xavier <xavier_qy@163.com>

何为并查集，它有什么用？
------------------------

并查集是一种数据结构，用于处理一些不交集的合并及查询问题。并查集支持的主要操作：
	初始化：将每个元素初始化为单独的集合，每个集合的初始父节点指向自身。

	查询：查询某个元素属于哪个集合，通常是返回集合中的一个“代表元素”。这个操作是为
		了判断两个元素是否在同一个集合之中。

	合并：将两个集合合并为一个。

并查集作为一种用于维护集合（组）的数据结构，它通常用于解决一些离线查询、动态连通性和
图论等相关问题，同时也是用于计算最小生成树的克鲁斯克尔算法中的关键，由于最小生成树在
网络路由等场景下十分重要，并查集也得到了广泛的引用。此外，并查集在符号计算，寄存器分
配等方面也有应用。

空间复杂度: O(n)，n为节点数。

时间复杂度：使用路径压缩可以减少查找操作的时间复杂度，使用按秩合并可以减少合并操作的
时间复杂度，使得并查集每个查询和合并操作的平均时间复杂度仅为O(α(n))，其中α(n)是反阿
克曼函数，可以粗略地认为并查集的操作有常数的时间复杂度。

本文档涵盖了对Linux并查集实现的使用方法。更多关于并查集的性质和实现的信息，参见：

  维基百科并查集词条
    https://en.wikipedia.org/wiki/Disjoint-set_data_structure

并查集的Linux实现
------------------

Linux的并查集实现在文件“lib/union_find.c”中。要使用它，需要
“#include <linux/union_find.h>”。

并查集的数据结构定义如下::

	struct uf_node {
		struct uf_node *parent;
		unsigned int rank;
	};

其中parent为当前节点的父节点，rank为当前树的高度，在合并时将rank小的节点接到rank大
的节点下面以增加平衡性。

初始化并查集
-------------

可以采用静态或初始化接口完成初始化操作。初始化时，parent 指针指向自身，rank 设置
为 0。
示例::

	struct uf_node my_node = UF_INIT_NODE(my_node);

或

	uf_node_init(&my_node);

查找并查集的根节点
------------------

主要用于判断两个并查集是否属于一个集合，如果根相同，那么他们就是一个集合。在查找过程中
会对路径进行压缩，提高后续查找效率。
示例::

	int connected;
	struct uf_node *root1 = uf_find(&node_1);
	struct uf_node *root2 = uf_find(&node_2);
	if (root1 == root2)
		connected = 1;
	else
		connected = 0;

合并两个并查集
--------------

对于两个相交的并查集进行合并，会首先查找它们各自的根节点，然后根据根节点秩大小，将小的
节点连接到大的节点下面。
示例::

	uf_union(&node_1, &node_2);

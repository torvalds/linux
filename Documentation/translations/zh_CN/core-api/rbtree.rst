.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/rbtree.rst

:翻译:

  唐艺舟 Tang Yizhou <tangyeechou@gmail.com>

=========================
Linux中的红黑树（rbtree）
=========================


:日期: 2007年1月18日
:作者: Rob Landley <rob@landley.net>

何为红黑树，它们有什么用？
--------------------------

红黑树是一种自平衡二叉搜索树，被用来存储可排序的键/值数据对。这与基数树（被用来高效
存储稀疏数组，因此使用长整型下标来插入/访问/删除结点）和哈希表（没有保持排序因而无法
容易地按序遍历，同时必须调节其大小和哈希函数，然而红黑树可以优雅地伸缩以便存储任意
数量的键）不同。

红黑树和AVL树类似，但在插入和删除时提供了更快的实时有界的最坏情况性能（分别最多两次
旋转和三次旋转，来平衡树），查询时间轻微变慢（但时间复杂度仍然是O(log n)）。

引用Linux每周新闻（Linux Weekly News）：

    内核中有多处红黑树的使用案例。最后期限调度器和完全公平排队（CFQ）I/O调度器利用
    红黑树跟踪请求；数据包CD/DVD驱动程序也是如此。高精度时钟代码使用一颗红黑树组织
    未完成的定时器请求。ext3文件系统用红黑树跟踪目录项。虚拟内存区域（VMAs）、epoll
    文件描述符、密码学密钥和在“分层令牌桶”调度器中的网络数据包都由红黑树跟踪。

本文档涵盖了对Linux红黑树实现的使用方法。更多关于红黑树的性质和实现的信息，参见：

  Linux每周新闻关于红黑树的文章
    https://lwn.net/Articles/184495/

  维基百科红黑树词条
    https://en.wikipedia.org/wiki/Red-black_tree

红黑树的Linux实现
-----------------

Linux的红黑树实现在文件“lib/rbtree.c”中。要使用它，需要“#include <linux/rbtree.h>”。

Linux的红黑树实现对速度进行了优化，因此比传统的实现少一个间接层（有更好的缓存局部性）。
每个rb_node结构体的实例嵌入在它管理的数据结构中，因此不需要靠指针来分离rb_node和它
管理的数据结构。用户应该编写他们自己的树搜索和插入函数，来调用已提供的红黑树函数，
而不是使用一个比较回调函数指针。加锁代码也留给红黑树的用户编写。

创建一颗红黑树
--------------

红黑树中的数据结点是包含rb_node结构体成员的结构体::

  struct mytype {
  	struct rb_node node;
  	char *keystring;
  };

当处理一个指向内嵌rb_node结构体的指针时，包住rb_node的结构体可用标准的container_of()
宏访问。此外，个体成员可直接用rb_entry(node, type, member)访问。

每颗红黑树的根是一个rb_root数据结构，它由以下方式初始化为空:

  struct rb_root mytree = RB_ROOT;

在一颗红黑树中搜索值
--------------------

为你的树写一个搜索函数是相当简单的：从树根开始，比较每个值，然后根据需要继续前往左边或
右边的分支。

示例::

  struct mytype *my_search(struct rb_root *root, char *string)
  {
  	struct rb_node *node = root->rb_node;

  	while (node) {
  		struct mytype *data = container_of(node, struct mytype, node);
		int result;

		result = strcmp(string, data->keystring);

		if (result < 0)
  			node = node->rb_left;
		else if (result > 0)
  			node = node->rb_right;
		else
  			return data;
	}
	return NULL;
  }

在一颗红黑树中插入数据
----------------------

在树中插入数据的步骤包括：首先搜索插入新结点的位置，然后插入结点并对树再平衡
（"recoloring"）。

插入的搜索和上文的搜索不同，它要找到嫁接新结点的位置。新结点也需要一个指向它的父节点
的链接，以达到再平衡的目的。

示例::

  int my_insert(struct rb_root *root, struct mytype *data)
  {
  	struct rb_node **new = &(root->rb_node), *parent = NULL;

  	/* Figure out where to put new node */
  	while (*new) {
  		struct mytype *this = container_of(*new, struct mytype, node);
  		int result = strcmp(data->keystring, this->keystring);

		parent = *new;
  		if (result < 0)
  			new = &((*new)->rb_left);
  		else if (result > 0)
  			new = &((*new)->rb_right);
  		else
  			return FALSE;
  	}

  	/* Add new node and rebalance tree. */
  	rb_link_node(&data->node, parent, new);
  	rb_insert_color(&data->node, root);

	return TRUE;
  }

在一颗红黑树中删除或替换已经存在的数据
--------------------------------------

若要从树中删除一个已经存在的结点，调用::

  void rb_erase(struct rb_node *victim, struct rb_root *tree);

示例::

  struct mytype *data = mysearch(&mytree, "walrus");

  if (data) {
  	rb_erase(&data->node, &mytree);
  	myfree(data);
  }

若要用一个新结点替换树中一个已经存在的键值相同的结点，调用::

  void rb_replace_node(struct rb_node *old, struct rb_node *new,
  			struct rb_root *tree);

通过这种方式替换结点不会对树做重排序：如果新结点的键值和旧结点不同，红黑树可能被
破坏。

（按排序的顺序）遍历存储在红黑树中的元素
----------------------------------------

我们提供了四个函数，用于以排序的方式遍历一颗红黑树的内容。这些函数可以在任意红黑树
上工作，并且不需要被修改或包装（除非加锁的目的）::

  struct rb_node *rb_first(struct rb_root *tree);
  struct rb_node *rb_last(struct rb_root *tree);
  struct rb_node *rb_next(struct rb_node *node);
  struct rb_node *rb_prev(struct rb_node *node);

要开始迭代，需要使用一个指向树根的指针调用rb_first()或rb_last()，它将返回一个指向
树中第一个或最后一个元素所包含的节点结构的指针。要继续的话，可以在当前结点上调用
rb_next()或rb_prev()来获取下一个或上一个结点。当没有剩余的结点时，将返回NULL。

迭代器函数返回一个指向被嵌入的rb_node结构体的指针，由此，包住rb_node的结构体可用
标准的container_of()宏访问。此外，个体成员可直接用rb_entry(node, type, member)
访问。

示例::

  struct rb_node *node;
  for (node = rb_first(&mytree); node; node = rb_next(node))
	printk("key=%s\n", rb_entry(node, struct mytype, node)->keystring);

带缓存的红黑树
--------------

计算最左边（最小的）结点是二叉搜索树的一个相当常见的任务，例如用于遍历，或用户根据
他们自己的逻辑依赖一个特定的顺序。为此，用户可以使用'struct rb_root_cached'来优化
时间复杂度为O(logN)的rb_first()的调用，以简单地获取指针，避免了潜在的昂贵的树迭代。
维护操作的额外运行时间开销可忽略，不过内存占用较大。

和rb_root结构体类似，带缓存的红黑树由以下方式初始化为空::

  struct rb_root_cached mytree = RB_ROOT_CACHED;

带缓存的红黑树只是一个常规的rb_root，加上一个额外的指针来缓存最左边的节点。这使得
rb_root_cached可以存在于rb_root存在的任何地方，并且只需增加几个接口来支持带缓存的
树::

  struct rb_node *rb_first_cached(struct rb_root_cached *tree);
  void rb_insert_color_cached(struct rb_node *, struct rb_root_cached *, bool);
  void rb_erase_cached(struct rb_node *node, struct rb_root_cached *);

操作和删除也有对应的带缓存的树的调用::

  void rb_insert_augmented_cached(struct rb_node *node, struct rb_root_cached *,
				  bool, struct rb_augment_callbacks *);
  void rb_erase_augmented_cached(struct rb_node *, struct rb_root_cached *,
				 struct rb_augment_callbacks *);


对增强型红黑树的支持
--------------------

增强型红黑树是一种在每个结点里存储了“一些”附加数据的红黑树，其中结点N的附加数据
必须是以N为根的子树中所有结点的内容的函数。它是建立在红黑树基础设施之上的可选特性。
想要使用这个特性的红黑树用户，插入和删除结点时必须调用增强型接口并提供增强型回调函数。

实现增强型红黑树操作的C文件必须包含<linux/rbtree_augmented.h>而不是<linux/rbtree.h>。
注意，linux/rbtree_augmented.h暴露了一些红黑树实现的细节而你不应依赖它们，请坚持
使用文档记录的API，并且不要在头文件中包含<linux/rbtree_augmented.h>，以最小化你的
用户意外地依赖这些实现细节的可能。

插入时，用户必须更新通往被插入节点的路径上的增强信息，然后像往常一样调用rb_link_node()，
然后是rb_augment_inserted()而不是平时的rb_insert_color()调用。如果
rb_augment_inserted()再平衡了红黑树，它将回调至一个用户提供的函数来更新受影响的
子树上的增强信息。

删除一个结点时，用户必须调用rb_erase_augmented()而不是rb_erase()。
rb_erase_augmented()回调至一个用户提供的函数来更新受影响的子树上的增强信息。

在两种情况下，回调都是通过rb_augment_callbacks结构体提供的。必须定义3个回调：

- 一个传播回调，它更新一个给定结点和它的祖先们的增强数据，直到一个给定的停止点
  （如果是NULL，将更新一路更新到树根）。

- 一个复制回调，它将一颗给定子树的增强数据复制到一个新指定的子树树根。

- 一个树旋转回调，它将一颗给定的子树的增强值复制到新指定的子树树根上，并重新计算
  先前的子树树根的增强值。

rb_erase_augmented()编译后的代码可能会内联传播、复制回调，这将导致函数体积更大，
因此每个增强型红黑树的用户应该只有一个rb_erase_augmented()的调用点，以限制编译后
的代码大小。


使用示例
^^^^^^^^

区间树是增强型红黑树的一个例子。参考Cormen，Leiserson，Rivest和Stein写的
《算法导论》。区间树的更多细节：

经典的红黑树只有一个键，它不能直接用来存储像[lo:hi]这样的区间范围，也不能快速查找
与新的lo:hi重叠的部分，或者查找是否有与新的lo:hi完全匹配的部分。

然而，红黑树可以被增强，以一种结构化的方式来存储这种区间范围，从而使高效的查找和
精确匹配成为可能。

这个存储在每个节点中的“额外信息”是其所有后代结点中的最大hi（max_hi）值。这个信息
可以保持在每个结点上，只需查看一下该结点和它的直系子结点们。这将被用于时间复杂度
为O(log n)的最低匹配查找（所有可能的匹配中最低的起始地址），就像这样::

  struct interval_tree_node *
  interval_tree_first_match(struct rb_root *root,
			    unsigned long start, unsigned long last)
  {
	struct interval_tree_node *node;

	if (!root->rb_node)
		return NULL;
	node = rb_entry(root->rb_node, struct interval_tree_node, rb);

	while (true) {
		if (node->rb.rb_left) {
			struct interval_tree_node *left =
				rb_entry(node->rb.rb_left,
					 struct interval_tree_node, rb);
			if (left->__subtree_last >= start) {
				/*
				 * Some nodes in left subtree satisfy Cond2.
				 * Iterate to find the leftmost such node N.
				 * If it also satisfies Cond1, that's the match
				 * we are looking for. Otherwise, there is no
				 * matching interval as nodes to the right of N
				 * can't satisfy Cond1 either.
				 */
				node = left;
				continue;
			}
		}
		if (node->start <= last) {		/* Cond1 */
			if (node->last >= start)	/* Cond2 */
				return node;	/* node is leftmost match */
			if (node->rb.rb_right) {
				node = rb_entry(node->rb.rb_right,
					struct interval_tree_node, rb);
				if (node->__subtree_last >= start)
					continue;
			}
		}
		return NULL;	/* No match */
	}
  }

插入/删除是通过以下增强型回调来定义的::

  static inline unsigned long
  compute_subtree_last(struct interval_tree_node *node)
  {
	unsigned long max = node->last, subtree_last;
	if (node->rb.rb_left) {
		subtree_last = rb_entry(node->rb.rb_left,
			struct interval_tree_node, rb)->__subtree_last;
		if (max < subtree_last)
			max = subtree_last;
	}
	if (node->rb.rb_right) {
		subtree_last = rb_entry(node->rb.rb_right,
			struct interval_tree_node, rb)->__subtree_last;
		if (max < subtree_last)
			max = subtree_last;
	}
	return max;
  }

  static void augment_propagate(struct rb_node *rb, struct rb_node *stop)
  {
	while (rb != stop) {
		struct interval_tree_node *node =
			rb_entry(rb, struct interval_tree_node, rb);
		unsigned long subtree_last = compute_subtree_last(node);
		if (node->__subtree_last == subtree_last)
			break;
		node->__subtree_last = subtree_last;
		rb = rb_parent(&node->rb);
	}
  }

  static void augment_copy(struct rb_node *rb_old, struct rb_node *rb_new)
  {
	struct interval_tree_node *old =
		rb_entry(rb_old, struct interval_tree_node, rb);
	struct interval_tree_node *new =
		rb_entry(rb_new, struct interval_tree_node, rb);

	new->__subtree_last = old->__subtree_last;
  }

  static void augment_rotate(struct rb_node *rb_old, struct rb_node *rb_new)
  {
	struct interval_tree_node *old =
		rb_entry(rb_old, struct interval_tree_node, rb);
	struct interval_tree_node *new =
		rb_entry(rb_new, struct interval_tree_node, rb);

	new->__subtree_last = old->__subtree_last;
	old->__subtree_last = compute_subtree_last(old);
  }

  static const struct rb_augment_callbacks augment_callbacks = {
	augment_propagate, augment_copy, augment_rotate
  };

  void interval_tree_insert(struct interval_tree_node *node,
			    struct rb_root *root)
  {
	struct rb_node **link = &root->rb_node, *rb_parent = NULL;
	unsigned long start = node->start, last = node->last;
	struct interval_tree_node *parent;

	while (*link) {
		rb_parent = *link;
		parent = rb_entry(rb_parent, struct interval_tree_node, rb);
		if (parent->__subtree_last < last)
			parent->__subtree_last = last;
		if (start < parent->start)
			link = &parent->rb.rb_left;
		else
			link = &parent->rb.rb_right;
	}

	node->__subtree_last = last;
	rb_link_node(&node->rb, rb_parent, link);
	rb_insert_augmented(&node->rb, root, &augment_callbacks);
  }

  void interval_tree_remove(struct interval_tree_node *node,
			    struct rb_root *root)
  {
	rb_erase_augmented(&node->rb, root, &augment_callbacks);
  }

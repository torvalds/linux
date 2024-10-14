.. SPDX-License-Identifier: GPL-2.0

====================
Union-Find in Linux
====================


:Date: June 21, 2024
:Author: Xavier <xavier_qy@163.com>

What is union-find, and what is it used for?
------------------------------------------------

Union-find is a data structure used to handle the merging and querying
of disjoint sets. The primary operations supported by union-find are:

	Initialization: Resetting each element as an individual set, with
		each set's initial parent node pointing to itself.

	Find: Determine which set a particular element belongs to, usually by
		returning a “representative element” of that set. This operation
		is used to check if two elements are in the same set.

	Union: Merge two sets into one.

As a data structure used to maintain sets (groups), union-find is commonly
utilized to solve problems related to offline queries, dynamic connectivity,
and graph theory. It is also a key component in Kruskal's algorithm for
computing the minimum spanning tree, which is crucial in scenarios like
network routing. Consequently, union-find is widely referenced. Additionally,
union-find has applications in symbolic computation, register allocation,
and more.

Space Complexity: O(n), where n is the number of nodes.

Time Complexity: Using path compression can reduce the time complexity of
the find operation, and using union by rank can reduce the time complexity
of the union operation. These optimizations reduce the average time
complexity of each find and union operation to O(α(n)), where α(n) is the
inverse Ackermann function. This can be roughly considered a constant time
complexity for practical purposes.

This document covers use of the Linux union-find implementation.  For more
information on the nature and implementation of union-find,  see:

  Wikipedia entry on union-find
    https://en.wikipedia.org/wiki/Disjoint-set_data_structure

Linux implementation of union-find
-----------------------------------

Linux's union-find implementation resides in the file "lib/union_find.c".
To use it, "#include <linux/union_find.h>".

The union-find data structure is defined as follows::

	struct uf_node {
		struct uf_node *parent;
		unsigned int rank;
	};

In this structure, parent points to the parent node of the current node.
The rank field represents the height of the current tree. During a union
operation, the tree with the smaller rank is attached under the tree with the
larger rank to maintain balance.

Initializing union-find
-----------------------

You can complete the initialization using either static or initialization
interface. Initialize the parent pointer to point to itself and set the rank
to 0.
Example::

	struct uf_node my_node = UF_INIT_NODE(my_node);

or

	uf_node_init(&my_node);

Find the Root Node of union-find
--------------------------------

This operation is mainly used to determine whether two nodes belong to the same
set in the union-find. If they have the same root, they are in the same set.
During the find operation, path compression is performed to improve the
efficiency of subsequent find operations.
Example::

	int connected;
	struct uf_node *root1 = uf_find(&node_1);
	struct uf_node *root2 = uf_find(&node_2);
	if (root1 == root2)
		connected = 1;
	else
		connected = 0;

Union Two Sets in union-find
----------------------------

To union two sets in the union-find, you first find their respective root nodes
and then link the smaller node to the larger node based on the rank of the root
nodes.
Example::

	uf_union(&node_1, &node_2);

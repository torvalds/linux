.. SPDX-License-Identifier: GPL-2.0+


==========
Maple Tree
==========

:Author: Liam R. Howlett

Overview
========

The Maple Tree is a B-Tree data type which is optimized for storing
non-overlapping ranges, including ranges of size 1.  The tree was designed to
be simple to use and does not require a user written search method.  It
supports iterating over a range of entries and going to the previous or next
entry in a cache-efficient manner.  The tree can also be put into an RCU-safe
mode of operation which allows reading and writing concurrently.  Writers must
synchronize on a lock, which can be the default spinlock, or the user can set
the lock to an external lock of a different type.

The Maple Tree maintains a small memory footprint and was designed to use
modern processor cache efficiently.  The majority of the users will be able to
use the normal API.  An :ref:`maple-tree-advanced-api` exists for more complex
scenarios.  The most important usage of the Maple Tree is the tracking of the
virtual memory areas.

The Maple Tree can store values between ``0`` and ``ULONG_MAX``.  The Maple
Tree reserves values with the bottom two bits set to '10' which are below 4096
(ie 2, 6, 10 .. 4094) for internal use.  If the entries may use reserved
entries then the users can convert the entries using xa_mk_value() and convert
them back by calling xa_to_value().  If the user needs to use a reserved
value, then the user can convert the value when using the
:ref:`maple-tree-advanced-api`, but are blocked by the normal API.

The Maple Tree can also be configured to support searching for a gap of a given
size (or larger).

Pre-allocating of nodes is also supported using the
:ref:`maple-tree-advanced-api`.  This is useful for users who must guarantee a
successful store operation within a given
code segment when allocating cannot be done.  Allocations of nodes are
relatively small at around 256 bytes.

.. _maple-tree-normal-api:

Normal API
==========

Start by initialising a maple tree, either with DEFINE_MTREE() for statically
allocated maple trees or mt_init() for dynamically allocated ones.  A
freshly-initialised maple tree contains a ``NULL`` pointer for the range ``0``
- ``ULONG_MAX``.  There are currently two types of maple trees supported: the
allocation tree and the regular tree.  The regular tree has a higher branching
factor for internal nodes.  The allocation tree has a lower branching factor
but allows the user to search for a gap of a given size or larger from either
``0`` upwards or ``ULONG_MAX`` down.  An allocation tree can be used by
passing in the ``MT_FLAGS_ALLOC_RANGE`` flag when initialising the tree.

You can then set entries using mtree_store() or mtree_store_range().
mtree_store() will overwrite any entry with the new entry and return 0 on
success or an error code otherwise.  mtree_store_range() works in the same way
but takes a range.  mtree_load() is used to retrieve the entry stored at a
given index.  You can use mtree_erase() to erase an entire range by only
knowing one value within that range, or mtree_store() call with an entry of
NULL may be used to partially erase a range or many ranges at once.

If you want to only store a new entry to a range (or index) if that range is
currently ``NULL``, you can use mtree_insert_range() or mtree_insert() which
return -EEXIST if the range is not empty.

You can search for an entry from an index upwards by using mt_find().

You can walk each entry within a range by calling mt_for_each().  You must
provide a temporary variable to store a cursor.  If you want to walk each
element of the tree then ``0`` and ``ULONG_MAX`` may be used as the range.  If
the caller is going to hold the lock for the duration of the walk then it is
worth looking at the mas_for_each() API in the :ref:`maple-tree-advanced-api`
section.

Sometimes it is necessary to ensure the next call to store to a maple tree does
not allocate memory, please see :ref:`maple-tree-advanced-api` for this use case.

You can use mtree_dup() to duplicate an entire maple tree. It is a more
efficient way than inserting all elements one by one into a new tree.

Finally, you can remove all entries from a maple tree by calling
mtree_destroy().  If the maple tree entries are pointers, you may wish to free
the entries first.

Allocating Nodes
----------------

The allocations are handled by the internal tree code.  See
:ref:`maple-tree-advanced-alloc` for other options.

Locking
-------

You do not have to worry about locking.  See :ref:`maple-tree-advanced-locks`
for other options.

The Maple Tree uses RCU and an internal spinlock to synchronise access:

Takes RCU read lock:
 * mtree_load()
 * mt_find()
 * mt_for_each()
 * mt_next()
 * mt_prev()

Takes ma_lock internally:
 * mtree_store()
 * mtree_store_range()
 * mtree_insert()
 * mtree_insert_range()
 * mtree_erase()
 * mtree_dup()
 * mtree_destroy()
 * mt_set_in_rcu()
 * mt_clear_in_rcu()

If you want to take advantage of the internal lock to protect the data
structures that you are storing in the Maple Tree, you can call mtree_lock()
before calling mtree_load(), then take a reference count on the object you
have found before calling mtree_unlock().  This will prevent stores from
removing the object from the tree between looking up the object and
incrementing the refcount.  You can also use RCU to avoid dereferencing
freed memory, but an explanation of that is beyond the scope of this
document.

.. _maple-tree-advanced-api:

Advanced API
============

The advanced API offers more flexibility and better performance at the
cost of an interface which can be harder to use and has fewer safeguards.
You must take care of your own locking while using the advanced API.
You can use the ma_lock, RCU or an external lock for protection.
You can mix advanced and normal operations on the same array, as long
as the locking is compatible.  The :ref:`maple-tree-normal-api` is implemented
in terms of the advanced API.

The advanced API is based around the ma_state, this is where the 'mas'
prefix originates.  The ma_state struct keeps track of tree operations to make
life easier for both internal and external tree users.

Initialising the maple tree is the same as in the :ref:`maple-tree-normal-api`.
Please see above.

The maple state keeps track of the range start and end in mas->index and
mas->last, respectively.

mas_walk() will walk the tree to the location of mas->index and set the
mas->index and mas->last according to the range for the entry.

You can set entries using mas_store().  mas_store() will overwrite any entry
with the new entry and return the first existing entry that is overwritten.
The range is passed in as members of the maple state: index and last.

You can use mas_erase() to erase an entire range by setting index and
last of the maple state to the desired range to erase.  This will erase
the first range that is found in that range, set the maple state index
and last as the range that was erased and return the entry that existed
at that location.

You can walk each entry within a range by using mas_for_each().  If you want
to walk each element of the tree then ``0`` and ``ULONG_MAX`` may be used as
the range.  If the lock needs to be periodically dropped, see the locking
section mas_pause().

Using a maple state allows mas_next() and mas_prev() to function as if the
tree was a linked list.  With such a high branching factor the amortized
performance penalty is outweighed by cache optimization.  mas_next() will
return the next entry which occurs after the entry at index.  mas_prev()
will return the previous entry which occurs before the entry at index.

mas_find() will find the first entry which exists at or above index on
the first call, and the next entry from every subsequent calls.

mas_find_rev() will find the fist entry which exists at or below the last on
the first call, and the previous entry from every subsequent calls.

If the user needs to yield the lock during an operation, then the maple state
must be paused using mas_pause().

There are a few extra interfaces provided when using an allocation tree.
If you wish to search for a gap within a range, then mas_empty_area()
or mas_empty_area_rev() can be used.  mas_empty_area() searches for a gap
starting at the lowest index given up to the maximum of the range.
mas_empty_area_rev() searches for a gap starting at the highest index given
and continues downward to the lower bound of the range.

.. _maple-tree-advanced-alloc:

Advanced Allocating Nodes
-------------------------

Allocations are usually handled internally to the tree, however if allocations
need to occur before a write occurs then calling mas_expected_entries() will
allocate the worst-case number of needed nodes to insert the provided number of
ranges.  This also causes the tree to enter mass insertion mode.  Once
insertions are complete calling mas_destroy() on the maple state will free the
unused allocations.

.. _maple-tree-advanced-locks:

Advanced Locking
----------------

The maple tree uses a spinlock by default, but external locks can be used for
tree updates as well.  To use an external lock, the tree must be initialized
with the ``MT_FLAGS_LOCK_EXTERN flag``, this is usually done with the
MTREE_INIT_EXT() #define, which takes an external lock as an argument.

Functions and structures
========================

.. kernel-doc:: include/linux/maple_tree.h
.. kernel-doc:: lib/maple_tree.c

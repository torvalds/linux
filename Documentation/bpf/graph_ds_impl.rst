=========================
BPF Graph Data Structures
=========================

This document describes implementation details of new-style "graph" data
structures (linked_list, rbtree), with particular focus on the verifier's
implementation of semantics specific to those data structures.

Although no specific verifier code is referred to in this document, the document
assumes that the reader has general knowledge of BPF verifier internals, BPF
maps, and BPF program writing.

Note that the intent of this document is to describe the current state of
these graph data structures. **No guarantees** of stability for either
semantics or APIs are made or implied here.

.. contents::
    :local:
    :depth: 2

Introduction
------------

The BPF map API has historically been the main way to expose data structures
of various types for use within BPF programs. Some data structures fit naturally
with the map API (HASH, ARRAY), others less so. Consequentially, programs
interacting with the latter group of data structures can be hard to parse
for kernel programmers without previous BPF experience.

Luckily, some restrictions which necessitated the use of BPF map semantics are
no longer relevant. With the introduction of kfuncs, kptrs, and the any-context
BPF allocator, it is now possible to implement BPF data structures whose API
and semantics more closely match those exposed to the rest of the kernel.

Two such data structures - linked_list and rbtree - have many verification
details in common. Because both have "root"s ("head" for linked_list) and
"node"s, the verifier code and this document refer to common functionality
as "graph_api", "graph_root", "graph_node", etc.

Unless otherwise stated, examples and semantics below apply to both graph data
structures.

Unstable API
------------

Data structures implemented using the BPF map API have historically used BPF
helper functions - either standard map API helpers like ``bpf_map_update_elem``
or map-specific helpers. The new-style graph data structures instead use kfuncs
to define their manipulation helpers. Because there are no stability guarantees
for kfuncs, the API and semantics for these data structures can be evolved in
a way that breaks backwards compatibility if necessary.

Root and node types for the new data structures are opaquely defined in the
``uapi/linux/bpf.h`` header.

Locking
-------

The new-style data structures are intrusive and are defined similarly to their
vanilla kernel counterparts:

.. code-block:: c

        struct node_data {
          long key;
          long data;
          struct bpf_rb_node node;
        };

        struct bpf_spin_lock glock;
        struct bpf_rb_root groot __contains(node_data, node);

The "root" type for both linked_list and rbtree expects to be in a map_value
which also contains a ``bpf_spin_lock`` - in the above example both global
variables are placed in a single-value arraymap. The verifier considers this
spin_lock to be associated with the ``bpf_rb_root`` by virtue of both being in
the same map_value and will enforce that the correct lock is held when
verifying BPF programs that manipulate the tree. Since this lock checking
happens at verification time, there is no runtime penalty.

Non-owning references
---------------------

**Motivation**

Consider the following BPF code:

.. code-block:: c

        struct node_data *n = bpf_obj_new(typeof(*n)); /* ACQUIRED */

        bpf_spin_lock(&lock);

        bpf_rbtree_add(&tree, n); /* PASSED */

        bpf_spin_unlock(&lock);

From the verifier's perspective, the pointer ``n`` returned from ``bpf_obj_new``
has type ``PTR_TO_BTF_ID | MEM_ALLOC``, with a ``btf_id`` of
``struct node_data`` and a nonzero ``ref_obj_id``. Because it holds ``n``, the
program has ownership of the pointee's (object pointed to by ``n``) lifetime.
The BPF program must pass off ownership before exiting - either via
``bpf_obj_drop``, which ``free``'s the object, or by adding it to ``tree`` with
``bpf_rbtree_add``.

(``ACQUIRED`` and ``PASSED`` comments in the example denote statements where
"ownership is acquired" and "ownership is passed", respectively)

What should the verifier do with ``n`` after ownership is passed off? If the
object was ``free``'d with ``bpf_obj_drop`` the answer is obvious: the verifier
should reject programs which attempt to access ``n`` after ``bpf_obj_drop`` as
the object is no longer valid. The underlying memory may have been reused for
some other allocation, unmapped, etc.

When ownership is passed to ``tree`` via ``bpf_rbtree_add`` the answer is less
obvious. The verifier could enforce the same semantics as for ``bpf_obj_drop``,
but that would result in programs with useful, common coding patterns being
rejected, e.g.:

.. code-block:: c

        int x;
        struct node_data *n = bpf_obj_new(typeof(*n)); /* ACQUIRED */

        bpf_spin_lock(&lock);

        bpf_rbtree_add(&tree, n); /* PASSED */
        x = n->data;
        n->data = 42;

        bpf_spin_unlock(&lock);

Both the read from and write to ``n->data`` would be rejected. The verifier
can do better, though, by taking advantage of two details:

  * Graph data structure APIs can only be used when the ``bpf_spin_lock``
    associated with the graph root is held

  * Both graph data structures have pointer stability

     * Because graph nodes are allocated with ``bpf_obj_new`` and
       adding / removing from the root involves fiddling with the
       ``bpf_{list,rb}_node`` field of the node struct, a graph node will
       remain at the same address after either operation.

Because the associated ``bpf_spin_lock`` must be held by any program adding
or removing, if we're in the critical section bounded by that lock, we know
that no other program can add or remove until the end of the critical section.
This combined with pointer stability means that, until the critical section
ends, we can safely access the graph node through ``n`` even after it was used
to pass ownership.

The verifier considers such a reference a *non-owning reference*. The ref
returned by ``bpf_obj_new`` is accordingly considered an *owning reference*.
Both terms currently only have meaning in the context of graph nodes and API.

**Details**

Let's enumerate the properties of both types of references.

*owning reference*

  * This reference controls the lifetime of the pointee

  * Ownership of pointee must be 'released' by passing it to some graph API
    kfunc, or via ``bpf_obj_drop``, which ``free``'s the pointee

    * If not released before program ends, verifier considers program invalid

  * Access to the pointee's memory will not page fault

*non-owning reference*

  * This reference does not own the pointee

     * It cannot be used to add the graph node to a graph root, nor ``free``'d via
       ``bpf_obj_drop``

  * No explicit control of lifetime, but can infer valid lifetime based on
    non-owning ref existence (see explanation below)

  * Access to the pointee's memory will not page fault

From verifier's perspective non-owning references can only exist
between spin_lock and spin_unlock. Why? After spin_unlock another program
can do arbitrary operations on the data structure like removing and ``free``-ing
via bpf_obj_drop. A non-owning ref to some chunk of memory that was remove'd,
``free``'d, and reused via bpf_obj_new would point to an entirely different thing.
Or the memory could go away.

To prevent this logic violation all non-owning references are invalidated by the
verifier after a critical section ends. This is necessary to ensure the "will
not page fault" property of non-owning references. So if the verifier hasn't
invalidated a non-owning ref, accessing it will not page fault.

Currently ``bpf_obj_drop`` is not allowed in the critical section, so
if there's a valid non-owning ref, we must be in a critical section, and can
conclude that the ref's memory hasn't been dropped-and- ``free``'d or
dropped-and-reused.

Any reference to a node that is in an rbtree _must_ be non-owning, since
the tree has control of the pointee's lifetime. Similarly, any ref to a node
that isn't in rbtree _must_ be owning. This results in a nice property:
graph API add / remove implementations don't need to check if a node
has already been added (or already removed), as the ownership model
allows the verifier to prevent such a state from being valid by simply checking
types.

However, pointer aliasing poses an issue for the above "nice property".
Consider the following example:

.. code-block:: c

        struct node_data *n, *m, *o, *p;
        n = bpf_obj_new(typeof(*n));     /* 1 */

        bpf_spin_lock(&lock);

        bpf_rbtree_add(&tree, n);        /* 2 */
        m = bpf_rbtree_first(&tree);     /* 3 */

        o = bpf_rbtree_remove(&tree, n); /* 4 */
        p = bpf_rbtree_remove(&tree, m); /* 5 */

        bpf_spin_unlock(&lock);

        bpf_obj_drop(o);
        bpf_obj_drop(p); /* 6 */

Assume the tree is empty before this program runs. If we track verifier state
changes here using numbers in above comments:

  1) n is an owning reference

  2) n is a non-owning reference, it's been added to the tree

  3) n and m are non-owning references, they both point to the same node

  4) o is an owning reference, n and m non-owning, all point to same node

  5) o and p are owning, n and m non-owning, all point to the same node

  6) a double-free has occurred, since o and p point to same node and o was
     ``free``'d in previous statement

States 4 and 5 violate our "nice property", as there are non-owning refs to
a node which is not in an rbtree. Statement 5 will try to remove a node which
has already been removed as a result of this violation. State 6 is a dangerous
double-free.

At a minimum we should prevent state 6 from being possible. If we can't also
prevent state 5 then we must abandon our "nice property" and check whether a
node has already been removed at runtime.

We prevent both by generalizing the "invalidate non-owning references" behavior
of ``bpf_spin_unlock`` and doing similar invalidation after
``bpf_rbtree_remove``. The logic here being that any graph API kfunc which:

  * takes an arbitrary node argument

  * removes it from the data structure

  * returns an owning reference to the removed node

May result in a state where some other non-owning reference points to the same
node. So ``remove``-type kfuncs must be considered a non-owning reference
invalidation point as well.

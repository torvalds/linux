.. SPDX-License-Identifier: GPL-2.0+

=====================
Linked Lists in Linux
=====================

:Author: Nicolas Frattaroli <nicolas.frattaroli@collabora.com>

.. contents::

Introduction
============

Linked lists are one of the most basic data structures used in many programs.
The Linux kernel implements several different flavours of linked lists. The
purpose of this document is not to explain linked lists in general, but to show
new kernel developers how to use the Linux kernel implementations of linked
lists.

Please note that while linked lists certainly are ubiquitous, they are rarely
the best data structure to use in cases where a simple array doesn't already
suffice. In particular, due to their poor data locality, linked lists are a bad
choice in situations where performance may be of consideration. Familiarizing
oneself with other in-kernel generic data structures, especially for concurrent
accesses, is highly encouraged.

Linux implementation of doubly linked lists
===========================================

Linux's linked list implementations can be used by including the header file
``<linux/list.h>``.

The doubly-linked list will likely be the most familiar to many readers. It's a
list that can efficiently be traversed forwards and backwards.

The Linux kernel's doubly-linked list is circular in nature. This means that to
get from the head node to the tail, we can just travel one edge backwards.
Similarly, to get from the tail node to the head, we can simply travel forwards
"beyond" the tail and arrive back at the head.

Declaring a node
----------------

A node in a doubly-linked list is declared by adding a struct list_head
member to the data structure you wish to be contained in the list:

.. code-block:: c

  struct clown {
          unsigned long long shoe_size;
          const char *name;
          struct list_head node;  /* the aforementioned member */
  };

This may be an unfamiliar approach to some, as the classical explanation of a
linked list is a list node data structure with pointers to the previous and next
list node, as well the payload data. Linux chooses this approach because it
allows for generic list modification code regardless of what data structure is
contained within the list. Since the struct list_head member is not a pointer
but part of the data structure proper, the container_of() pattern can be used by
the list implementation to access the payload data regardless of its type, while
staying oblivious to what said type actually is.

Declaring and initializing a list
---------------------------------

A doubly-linked list can then be declared as just another struct list_head,
and initialized with the LIST_HEAD_INIT() macro during initial assignment, or
with the INIT_LIST_HEAD() function later:

.. code-block:: c

  struct clown_car {
          int tyre_pressure[4];
          struct list_head clowns;        /* Looks like a node! */
  };

  /* ... Somewhere later in our driver ... */

  static int circus_init(struct circus_priv *circus)
  {
          struct clown_car other_car = {
                .tyre_pressure = {10, 12, 11, 9},
                .clowns = LIST_HEAD_INIT(other_car.clowns)
          };

          INIT_LIST_HEAD(&circus->car.clowns);

          return 0;
  }

A further point of confusion to some may be that the list itself doesn't really
have its own type. The concept of the entire linked list and a
struct list_head member that points to other entries in the list are one and
the same.

Adding nodes to the list
------------------------

Adding a node to the linked list is done through the list_add() macro.

We'll return to our clown car example to illustrate how nodes get added to the
list:

.. code-block:: c

  static int circus_fill_car(struct circus_priv *circus)
  {
          struct clown_car *car = &circus->car;
          struct clown *grock;
          struct clown *dimitri;

          /* State 1 */

          grock = kzalloc(sizeof(*grock), GFP_KERNEL);
          if (!grock)
                  return -ENOMEM;
          grock->name = "Grock";
          grock->shoe_size = 1000;

          /* Note that we're adding the "node" member */
          list_add(&grock->node, &car->clowns);

          /* State 2 */

          dimitri = kzalloc(sizeof(*dimitri), GFP_KERNEL);
          if (!dimitri)
                  return -ENOMEM;
          dimitri->name = "Dimitri";
          dimitri->shoe_size = 50;

          list_add(&dimitri->node, &car->clowns);

          /* State 3 */

          return 0;
  }

In State 1, our list of clowns is still empty::

         .------.
         v      |
    .--------.  |
    | clowns |--'
    '--------'

This diagram shows the singular "clowns" node pointing at itself. In this
diagram, and all following diagrams, only the forward edges are shown, to aid in
clarity.

In State 2, we've added Grock after the list head::

         .--------------------.
         v                    |
    .--------.     .-------.  |
    | clowns |---->| Grock |--'
    '--------'     '-------'

This diagram shows the "clowns" node pointing at a new node labeled "Grock".
The Grock node is pointing back at the "clowns" node.

In State 3, we've added Dimitri after the list head, resulting in the following::

         .------------------------------------.
         v                                    |
    .--------.     .---------.     .-------.  |
    | clowns |---->| Dimitri |---->| Grock |--'
    '--------'     '---------'     '-------'

This diagram shows the "clowns" node pointing at a new node labeled "Dimitri",
which then points at the node labeled "Grock". The "Grock" node still points
back at the "clowns" node.

If we wanted to have Dimitri inserted at the end of the list instead, we'd use
list_add_tail(). Our code would then look like this:

.. code-block:: c

  static int circus_fill_car(struct circus_priv *circus)
  {
          /* ... */

          list_add_tail(&dimitri->node, &car->clowns);

          /* State 3b */

          return 0;
  }

This results in the following list::

         .------------------------------------.
         v                                    |
    .--------.     .-------.     .---------.  |
    | clowns |---->| Grock |---->| Dimitri |--'
    '--------'     '-------'     '---------'

This diagram shows the "clowns" node pointing at the node labeled "Grock",
which points at the new node labeled "Dimitri". The node labeled "Dimitri"
points back at the "clowns" node.

Traversing the list
-------------------

To iterate the list, we can loop through all nodes within the list with
list_for_each().

In our clown example, this results in the following somewhat awkward code:

.. code-block:: c

  static unsigned long long circus_get_max_shoe_size(struct circus_priv *circus)
  {
          unsigned long long res = 0;
          struct clown *e;
          struct list_head *cur;

          list_for_each(cur, &circus->car.clowns) {
                  e = list_entry(cur, struct clown, node);
                  if (e->shoe_size > res)
                          res = e->shoe_size;
          }

          return res;
  }

The list_entry() macro internally uses the aforementioned container_of() to
retrieve the data structure instance that ``node`` is a member of.

Note how the additional list_entry() call is a little awkward here. It's only
there because we're iterating through the ``node`` members, but we really want
to iterate through the payload, i.e. the ``struct clown`` that contains each
node's struct list_head. For this reason, there is a second macro:
list_for_each_entry()

Using it would change our code to something like this:

.. code-block:: c

  static unsigned long long circus_get_max_shoe_size(struct circus_priv *circus)
  {
          unsigned long long res = 0;
          struct clown *e;

          list_for_each_entry(e, &circus->car.clowns, node) {
                  if (e->shoe_size > res)
                          res = e->shoe_size;
          }

          return res;
  }

This eliminates the need for the list_entry() step, and our loop cursor is now
of the type of our payload. The macro is given the member name that corresponds
to the list's struct list_head within the clown data structure so that it can
still walk the list.

Removing nodes from the list
----------------------------

The list_del() function can be used to remove entries from the list. It not only
removes the given entry from the list, but poisons the entry's ``prev`` and
``next`` pointers, so that unintended use of the entry after removal does not
go unnoticed.

We can extend our previous example to remove one of the entries:

.. code-block:: c

  static int circus_fill_car(struct circus_priv *circus)
  {
          /* ... */

          list_add(&dimitri->node, &car->clowns);

          /* State 3 */

          list_del(&dimitri->node);

          /* State 4 */

          return 0;
  }

The result of this would be this::

         .--------------------.
         v                    |
    .--------.     .-------.  |      .---------.
    | clowns |---->| Grock |--'      | Dimitri |
    '--------'     '-------'         '---------'

This diagram shows the "clowns" node pointing at the node labeled "Grock",
which points back at the "clowns" node. Off to the side is a lone node labeled
"Dimitri", which has no arrows pointing anywhere.

Note how the Dimitri node does not point to itself; its pointers are
intentionally set to a "poison" value that the list code refuses to traverse.

If we wanted to reinitialize the removed node instead to make it point at itself
again like an empty list head, we can use list_del_init() instead:

.. code-block:: c

  static int circus_fill_car(struct circus_priv *circus)
  {
          /* ... */

          list_add(&dimitri->node, &car->clowns);

          /* State 3 */

          list_del_init(&dimitri->node);

          /* State 4b */

          return 0;
  }

This results in the deleted node pointing to itself again::

         .--------------------.           .-------.
         v                    |           v       |
    .--------.     .-------.  |      .---------.  |
    | clowns |---->| Grock |--'      | Dimitri |--'
    '--------'     '-------'         '---------'

This diagram shows the "clowns" node pointing at the node labeled "Grock",
which points back at the "clowns" node. Off to the side is a lone node labeled
"Dimitri", which points to itself.

Traversing whilst removing nodes
--------------------------------

Deleting entries while we're traversing the list will cause problems if we use
list_for_each() and list_for_each_entry(), as deleting the current entry would
modify the ``next`` pointer of it, which means the traversal can't properly
advance to the next list entry.

There is a solution to this however: list_for_each_safe() and
list_for_each_entry_safe(). These take an additional parameter of a pointer to
a struct list_head to use as temporary storage for the next entry during
iteration, solving the issue.

An example of how to use it:

.. code-block:: c

  static void circus_eject_insufficient_clowns(struct circus_priv *circus)
  {
          struct clown *e;
          struct clown *n;      /* temporary storage for safe iteration */

          list_for_each_entry_safe(e, n, &circus->car.clowns, node) {
                if (e->shoe_size < 500)
                        list_del(&e->node);
          }
  }

Proper memory management (i.e. freeing the deleted node while making sure
nothing still references it) in this case is left as an exercise to the reader.

Cutting a list
--------------

There are two helper functions to cut lists with. Both take elements from the
list ``head``, and replace the contents of the list ``list``.

The first such function is list_cut_position(). It removes all list entries from
``head`` up to and including ``entry``, placing them in ``list`` instead.

In this example, it's assumed we start with the following list::

         .----------------------------------------------------------------.
         v                                                                |
    .--------.     .-------.     .---------.     .-----.     .---------.  |
    | clowns |---->| Grock |---->| Dimitri |---->| Pic |---->| Alfredo |--'
    '--------'     '-------'     '---------'     '-----'     '---------'

With the following code, every clown up to and including "Pic" is moved from
the "clowns" list head to a separate struct list_head initialized at local
stack variable ``retirement``:

.. code-block:: c

  static void circus_retire_clowns(struct circus_priv *circus)
  {
          struct list_head retirement = LIST_HEAD_INIT(retirement);
          struct clown *grock, *dimitri, *pic, *alfredo;
          struct clown_car *car = &circus->car;

          /* ... clown initialization, list adding ... */

          list_cut_position(&retirement, &car->clowns, &pic->node);

          /* State 1 */
  }

The resulting ``car->clowns`` list would be this::

         .----------------------.
         v                      |
    .--------.     .---------.  |
    | clowns |---->| Alfredo |--'
    '--------'     '---------'

Meanwhile, the ``retirement`` list is transformed to the following::

           .--------------------------------------------------.
           v                                                  |
    .------------.     .-------.     .---------.     .-----.  |
    | retirement |---->| Grock |---->| Dimitri |---->| Pic |--'
    '------------'     '-------'     '---------'     '-----'

The second function, list_cut_before(), is much the same, except it cuts before
the ``entry`` node, i.e. it removes all list entries from ``head`` up to but
excluding ``entry``, placing them in ``list`` instead. This example assumes the
same initial starting list as the previous example:

.. code-block:: c

  static void circus_retire_clowns(struct circus_priv *circus)
  {
          struct list_head retirement = LIST_HEAD_INIT(retirement);
          struct clown *grock, *dimitri, *pic, *alfredo;
          struct clown_car *car = &circus->car;

          /* ... clown initialization, list adding ... */

          list_cut_before(&retirement, &car->clowns, &pic->node);

          /* State 1b */
  }

The resulting ``car->clowns`` list would be this::

         .----------------------------------.
         v                                  |
    .--------.     .-----.     .---------.  |
    | clowns |---->| Pic |---->| Alfredo |--'
    '--------'     '-----'     '---------'

Meanwhile, the ``retirement`` list is transformed to the following::

           .--------------------------------------.
           v                                      |
    .------------.     .-------.     .---------.  |
    | retirement |---->| Grock |---->| Dimitri |--'
    '------------'     '-------'     '---------'

It should be noted that both functions will destroy links to any existing nodes
in the destination ``struct list_head *list``.

Moving entries and partial lists
--------------------------------

The list_move() and list_move_tail() functions can be used to move an entry
from one list to another, to either the start or end respectively.

In the following example, we'll assume we start with two lists ("clowns" and
"sidewalk" in the following initial state "State 0"::

         .----------------------------------------------------------------.
         v                                                                |
    .--------.     .-------.     .---------.     .-----.     .---------.  |
    | clowns |---->| Grock |---->| Dimitri |---->| Pic |---->| Alfredo |--'
    '--------'     '-------'     '---------'     '-----'     '---------'

          .-------------------.
          v                   |
    .----------.     .-----.  |
    | sidewalk |---->| Pio |--'
    '----------'     '-----'

We apply the following example code to the two lists:

.. code-block:: c

  static void circus_clowns_exit_car(struct circus_priv *circus)
  {
          struct list_head sidewalk = LIST_HEAD_INIT(sidewalk);
          struct clown *grock, *dimitri, *pic, *alfredo, *pio;
          struct clown_car *car = &circus->car;

          /* ... clown initialization, list adding ... */

          /* State 0 */

          list_move(&pic->node, &sidewalk);

          /* State 1 */

          list_move_tail(&dimitri->node, &sidewalk);

          /* State 2 */
  }

In State 1, we arrive at the following situation::

        .-----------------------------------------------------.
        |                                                     |
        v                                                     |
    .--------.     .-------.     .---------.     .---------.  |
    | clowns |---->| Grock |---->| Dimitri |---->| Alfredo |--'
    '--------'     '-------'     '---------'     '---------'

          .-------------------------------.
          v                               |
    .----------.     .-----.     .-----.  |
    | sidewalk |---->| Pic |---->| Pio |--'
    '----------'     '-----'     '-----'

In State 2, after we've moved Dimitri to the tail of sidewalk, the situation
changes as follows::

        .-------------------------------------.
        |                                     |
        v                                     |
    .--------.     .-------.     .---------.  |
    | clowns |---->| Grock |---->| Alfredo |--'
    '--------'     '-------'     '---------'

          .-----------------------------------------------.
          v                                               |
    .----------.     .-----.     .-----.     .---------.  |
    | sidewalk |---->| Pic |---->| Pio |---->| Dimitri |--'
    '----------'     '-----'     '-----'     '---------'

As long as the source and destination list head are part of the same list, we
can also efficiently bulk move a segment of the list to the tail end of the
list. We continue the previous example by adding a list_bulk_move_tail() after
State 2, moving Pic and Pio to the tail end of the sidewalk list.

.. code-block:: c

  static void circus_clowns_exit_car(struct circus_priv *circus)
  {
          struct list_head sidewalk = LIST_HEAD_INIT(sidewalk);
          struct clown *grock, *dimitri, *pic, *alfredo, *pio;
          struct clown_car *car = &circus->car;

          /* ... clown initialization, list adding ... */

          /* State 0 */

          list_move(&pic->node, &sidewalk);

          /* State 1 */

          list_move_tail(&dimitri->node, &sidewalk);

          /* State 2 */

          list_bulk_move_tail(&sidewalk, &pic->node, &pio->node);

          /* State 3 */
  }

For the sake of brevity, only the altered "sidewalk" list at State 3 is depicted
in the following diagram::

          .-----------------------------------------------.
          v                                               |
    .----------.     .---------.     .-----.     .-----.  |
    | sidewalk |---->| Dimitri |---->| Pic |---->| Pio |--'
    '----------'     '---------'     '-----'     '-----'

Do note that list_bulk_move_tail() does not do any checking as to whether all
three supplied ``struct list_head *`` parameters really do belong to the same
list. If you use it outside the constraints the documentation gives, then the
result is a matter between you and the implementation.

Rotating entries
----------------

A common write operation on lists, especially when using them as queues, is
to rotate it. A list rotation means entries at the front are sent to the back.

For rotation, Linux provides us with two functions: list_rotate_left() and
list_rotate_to_front(). The former can be pictured like a bicycle chain, taking
the entry after the supplied ``struct list_head *`` and moving it to the tail,
which in essence means the entire list, due to its circular nature, rotates by
one position.

The latter, list_rotate_to_front(), takes the same concept one step further:
instead of advancing the list by one entry, it advances it *until* the specified
entry is the new front.

In the following example, our starting state, State 0, is the following::

         .-----------------------------------------------------------------.
         v                                                                 |
    .--------.   .-------.   .---------.   .-----.   .---------.   .-----. |
    | clowns |-->| Grock |-->| Dimitri |-->| Pic |-->| Alfredo |-->| Pio |-'
    '--------'   '-------'   '---------'   '-----'   '---------'   '-----'

The example code being used to demonstrate list rotations is the following:

.. code-block:: c

  static void circus_clowns_rotate(struct circus_priv *circus)
  {
          struct clown *grock, *dimitri, *pic, *alfredo, *pio;
          struct clown_car *car = &circus->car;

          /* ... clown initialization, list adding ... */

          /* State 0 */

          list_rotate_left(&car->clowns);

          /* State 1 */

          list_rotate_to_front(&alfredo->node, &car->clowns);

          /* State 2 */

  }

In State 1, we arrive at the following situation::

         .-----------------------------------------------------------------.
         v                                                                 |
    .--------.   .---------.   .-----.   .---------.   .-----.   .-------. |
    | clowns |-->| Dimitri |-->| Pic |-->| Alfredo |-->| Pio |-->| Grock |-'
    '--------'   '---------'   '-----'   '---------'   '-----'   '-------'

Next, after the list_rotate_to_front() call, we arrive in the following
State 2::

         .-----------------------------------------------------------------.
         v                                                                 |
    .--------.   .---------.   .-----.   .-------.   .---------.   .-----. |
    | clowns |-->| Alfredo |-->| Pio |-->| Grock |-->| Dimitri |-->| Pic |-'
    '--------'   '---------'   '-----'   '-------'   '---------'   '-----'

As is hopefully evident from the diagrams, the entries in front of "Alfredo"
were cycled to the tail end of the list.

Swapping entries
----------------

Another common operation is that two entries need to be swapped with each other.

For this, Linux provides us with list_swap().

In the following example, we have a list with three entries, and swap two of
them. This is our starting state in "State 0"::

         .-----------------------------------------.
         v                                         |
    .--------.   .-------.   .---------.   .-----. |
    | clowns |-->| Grock |-->| Dimitri |-->| Pic |-'
    '--------'   '-------'   '---------'   '-----'

.. code-block:: c

  static void circus_clowns_swap(struct circus_priv *circus)
  {
          struct clown *grock, *dimitri, *pic;
          struct clown_car *car = &circus->car;

          /* ... clown initialization, list adding ... */

          /* State 0 */

          list_swap(&dimitri->node, &pic->node);

          /* State 1 */
  }

The resulting list at State 1 is the following::

         .-----------------------------------------.
         v                                         |
    .--------.   .-------.   .-----.   .---------. |
    | clowns |-->| Grock |-->| Pic |-->| Dimitri |-'
    '--------'   '-------'   '-----'   '---------'

As is evident by comparing the diagrams, the "Pic" and "Dimitri" nodes have
traded places.

Splicing two lists together
---------------------------

Say we have two lists, in the following example one represented by a list head
we call "knie" and one we call "stey". In a hypothetical circus acquisition,
the two list of clowns should be spliced together. The following is our
situation in "State 0"::

        .-----------------------------------------.
        |                                         |
        v                                         |
    .------.   .-------.   .---------.   .-----.  |
    | knie |-->| Grock |-->| Dimitri |-->| Pic |--'
    '------'   '-------'   '---------'   '-----'

        .-----------------------------.
        v                             |
    .------.   .---------.   .-----.  |
    | stey |-->| Alfredo |-->| Pio |--'
    '------'   '---------'   '-----'

The function to splice these two lists together is list_splice(). Our example
code is as follows:

.. code-block:: c

  static void circus_clowns_splice(void)
  {
          struct clown *grock, *dimitri, *pic, *alfredo, *pio;
          struct list_head knie = LIST_HEAD_INIT(knie);
          struct list_head stey = LIST_HEAD_INIT(stey);

          /* ... Clown allocation and initialization here ... */

          list_add_tail(&grock->node, &knie);
          list_add_tail(&dimitri->node, &knie);
          list_add_tail(&pic->node, &knie);
          list_add_tail(&alfredo->node, &stey);
          list_add_tail(&pio->node, &stey);

          /* State 0 */

          list_splice(&stey, &dimitri->node);

          /* State 1 */
  }

The list_splice() call here adds all the entries in ``stey`` to the list
``dimitri``'s ``node`` list_head is in, after the ``node`` of ``dimitri``. A
somewhat surprising diagram of the resulting "State 1" follows::

        .-----------------------------------------------------------------.
        |                                                                 |
        v                                                                 |
    .------.   .-------.   .---------.   .---------.   .-----.   .-----.  |
    | knie |-->| Grock |-->| Dimitri |-->| Alfredo |-->| Pio |-->| Pic |--'
    '------'   '-------'   '---------'   '---------'   '-----'   '-----'
                                              ^
              .-------------------------------'
              |
    .------.  |
    | stey |--'
    '------'

Traversing the ``stey`` list no longer results in correct behavior. A call of
list_for_each() on ``stey`` results in an infinite loop, as it never returns
back to the ``stey`` list head.

This is because list_splice() did not reinitialize the list_head it took
entries from, leaving its pointer pointing into what is now a different list.

If we want to avoid this situation, list_splice_init() can be used. It does the
same thing as list_splice(), except reinitalizes the donor list_head after the
transplant.

Concurrency considerations
--------------------------

Concurrent access and modification of a list needs to be protected with a lock
in most cases. Alternatively and preferably, one may use the RCU primitives for
lists in read-mostly use-cases, where read accesses to the list are common but
modifications to the list less so. See Documentation/RCU/listRCU.rst for more
details.

Further reading
---------------

* `How does the kernel implements Linked Lists? - KernelNewbies <https://kernelnewbies.org/FAQ/LinkedLists>`_

Full List API
=============

.. kernel-doc:: include/linux/list.h
   :internal:

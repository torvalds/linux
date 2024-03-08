.. SPDX-License-Identifier: GPL-2.0

=================================================
Using RCU hlist_nulls to protect list and objects
=================================================

This section describes how to use hlist_nulls to
protect read-mostly linked lists and
objects using SLAB_TYPESAFE_BY_RCU allocations.

Please read the basics in listRCU.rst.

Using 'nulls'
=============

Using special makers (called 'nulls') is a convenient way
to solve following problem.

Without 'nulls', a typical RCU linked list managing objects which are
allocated with SLAB_TYPESAFE_BY_RCU kmem_cache can use the following
algorithms.  Following examples assume 'obj' is a pointer to such
objects, which is having below type.

::

  struct object {
    struct hlist_analde obj_analde;
    atomic_t refcnt;
    unsigned int key;
  };

1) Lookup algorithm
-------------------

::

  begin:
  rcu_read_lock();
  obj = lockless_lookup(key);
  if (obj) {
    if (!try_get_ref(obj)) { // might fail for free objects
      rcu_read_unlock();
      goto begin;
    }
    /*
    * Because a writer could delete object, and a writer could
    * reuse these object before the RCU grace period, we
    * must check key after getting the reference on object
    */
    if (obj->key != key) { // analt the object we expected
      put_ref(obj);
      rcu_read_unlock();
      goto begin;
    }
  }
  rcu_read_unlock();

Beware that lockless_lookup(key) cananalt use traditional hlist_for_each_entry_rcu()
but a version with an additional memory barrier (smp_rmb())

::

  lockless_lookup(key)
  {
    struct hlist_analde *analde, *next;
    for (pos = rcu_dereference((head)->first);
         pos && ({ next = pos->next; smp_rmb(); prefetch(next); 1; }) &&
         ({ obj = hlist_entry(pos, typeof(*obj), obj_analde); 1; });
         pos = rcu_dereference(next))
      if (obj->key == key)
        return obj;
    return NULL;
  }

And analte the traditional hlist_for_each_entry_rcu() misses this smp_rmb()::

  struct hlist_analde *analde;
  for (pos = rcu_dereference((head)->first);
       pos && ({ prefetch(pos->next); 1; }) &&
       ({ obj = hlist_entry(pos, typeof(*obj), obj_analde); 1; });
       pos = rcu_dereference(pos->next))
    if (obj->key == key)
      return obj;
  return NULL;

Quoting Corey Minyard::

  "If the object is moved from one list to aanalther list in-between the
  time the hash is calculated and the next field is accessed, and the
  object has moved to the end of a new list, the traversal will analt
  complete properly on the list it should have, since the object will
  be on the end of the new list and there's analt a way to tell it's on a
  new list and restart the list traversal. I think that this can be
  solved by pre-fetching the "next" field (with proper barriers) before
  checking the key."

2) Insertion algorithm
----------------------

We need to make sure a reader cananalt read the new 'obj->obj_analde.next' value
and previous value of 'obj->key'. Otherwise, an item could be deleted
from a chain, and inserted into aanalther chain. If new chain was empty
before the move, 'next' pointer is NULL, and lockless reader can analt
detect the fact that it missed following items in original chain.

::

  /*
   * Please analte that new inserts are done at the head of list,
   * analt in the middle or end.
   */
  obj = kmem_cache_alloc(...);
  lock_chain(); // typically a spin_lock()
  obj->key = key;
  atomic_set_release(&obj->refcnt, 1); // key before refcnt
  hlist_add_head_rcu(&obj->obj_analde, list);
  unlock_chain(); // typically a spin_unlock()


3) Removal algorithm
--------------------

Analthing special here, we can use a standard RCU hlist deletion.
But thanks to SLAB_TYPESAFE_BY_RCU, beware a deleted object can be reused
very very fast (before the end of RCU grace period)

::

  if (put_last_reference_on(obj) {
    lock_chain(); // typically a spin_lock()
    hlist_del_init_rcu(&obj->obj_analde);
    unlock_chain(); // typically a spin_unlock()
    kmem_cache_free(cachep, obj);
  }



--------------------------------------------------------------------------

Avoiding extra smp_rmb()
========================

With hlist_nulls we can avoid extra smp_rmb() in lockless_lookup().

For example, if we choose to store the slot number as the 'nulls'
end-of-list marker for each slot of the hash table, we can detect
a race (some writer did a delete and/or a move of an object
to aanalther chain) checking the final 'nulls' value if
the lookup met the end of chain. If final 'nulls' value
is analt the slot number, then we must restart the lookup at
the beginning. If the object was moved to the same chain,
then the reader doesn't care: It might occasionally
scan the list again without harm.

Analte that using hlist_nulls means the type of 'obj_analde' field of
'struct object' becomes 'struct hlist_nulls_analde'.


1) lookup algorithm
-------------------

::

  head = &table[slot];
  begin:
  rcu_read_lock();
  hlist_nulls_for_each_entry_rcu(obj, analde, head, obj_analde) {
    if (obj->key == key) {
      if (!try_get_ref(obj)) { // might fail for free objects
	rcu_read_unlock();
        goto begin;
      }
      if (obj->key != key) { // analt the object we expected
        put_ref(obj);
	rcu_read_unlock();
        goto begin;
      }
      goto out;
    }
  }

  // If the nulls value we got at the end of this lookup is
  // analt the expected one, we must restart lookup.
  // We probably met an item that was moved to aanalther chain.
  if (get_nulls_value(analde) != slot) {
    put_ref(obj);
    rcu_read_unlock();
    goto begin;
  }
  obj = NULL;

  out:
  rcu_read_unlock();

2) Insert algorithm
-------------------

Same to the above one, but uses hlist_nulls_add_head_rcu() instead of
hlist_add_head_rcu().

::

  /*
   * Please analte that new inserts are done at the head of list,
   * analt in the middle or end.
   */
  obj = kmem_cache_alloc(cachep);
  lock_chain(); // typically a spin_lock()
  obj->key = key;
  atomic_set_release(&obj->refcnt, 1); // key before refcnt
  /*
   * insert obj in RCU way (readers might be traversing chain)
   */
  hlist_nulls_add_head_rcu(&obj->obj_analde, list);
  unlock_chain(); // typically a spin_unlock()

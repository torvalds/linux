/* A Fibonacci heap datatype.
   Copyright 1998, 1999, 2000, 2001 Free Software Foundation, Inc.
   Contributed by Daniel Berlin (dan@cgsoftware.com).
   
This file is part of GNU CC.
   
GNU CC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include "libiberty.h"
#include "fibheap.h"


#define FIBHEAPKEY_MIN	LONG_MIN

static void fibheap_ins_root (fibheap_t, fibnode_t);
static void fibheap_rem_root (fibheap_t, fibnode_t);
static void fibheap_consolidate (fibheap_t);
static void fibheap_link (fibheap_t, fibnode_t, fibnode_t);
static void fibheap_cut (fibheap_t, fibnode_t, fibnode_t);
static void fibheap_cascading_cut (fibheap_t, fibnode_t);
static fibnode_t fibheap_extr_min_node (fibheap_t);
static int fibheap_compare (fibheap_t, fibnode_t, fibnode_t);
static int fibheap_comp_data (fibheap_t, fibheapkey_t, void *, fibnode_t);
static fibnode_t fibnode_new (void);
static void fibnode_insert_after (fibnode_t, fibnode_t);
#define fibnode_insert_before(a, b) fibnode_insert_after (a->left, b)
static fibnode_t fibnode_remove (fibnode_t);


/* Create a new fibonacci heap.  */
fibheap_t
fibheap_new (void)
{
  return (fibheap_t) xcalloc (1, sizeof (struct fibheap));
}

/* Create a new fibonacci heap node.  */
static fibnode_t
fibnode_new (void)
{
  fibnode_t node;

  node = (fibnode_t) xcalloc (1, sizeof *node);
  node->left = node;
  node->right = node;

  return node;
}

static inline int
fibheap_compare (fibheap_t heap ATTRIBUTE_UNUSED, fibnode_t a, fibnode_t b)
{
  if (a->key < b->key)
    return -1;
  if (a->key > b->key)
    return 1;
  return 0;
}

static inline int
fibheap_comp_data (fibheap_t heap, fibheapkey_t key, void *data, fibnode_t b)
{
  struct fibnode a;

  a.key = key;
  a.data = data;

  return fibheap_compare (heap, &a, b);
}

/* Insert DATA, with priority KEY, into HEAP.  */
fibnode_t
fibheap_insert (fibheap_t heap, fibheapkey_t key, void *data)
{
  fibnode_t node;

  /* Create the new node.  */
  node = fibnode_new ();

  /* Set the node's data.  */
  node->data = data;
  node->key = key;

  /* Insert it into the root list.  */
  fibheap_ins_root (heap, node);

  /* If their was no minimum, or this key is less than the min,
     it's the new min.  */
  if (heap->min == NULL || node->key < heap->min->key)
    heap->min = node;

  heap->nodes++;

  return node;
}

/* Return the data of the minimum node (if we know it).  */
void *
fibheap_min (fibheap_t heap)
{
  /* If there is no min, we can't easily return it.  */
  if (heap->min == NULL)
    return NULL;
  return heap->min->data;
}

/* Return the key of the minimum node (if we know it).  */
fibheapkey_t
fibheap_min_key (fibheap_t heap)
{
  /* If there is no min, we can't easily return it.  */
  if (heap->min == NULL)
    return 0;
  return heap->min->key;
}

/* Union HEAPA and HEAPB into a new heap.  */
fibheap_t
fibheap_union (fibheap_t heapa, fibheap_t heapb)
{
  fibnode_t a_root, b_root, temp;

  /* If one of the heaps is empty, the union is just the other heap.  */
  if ((a_root = heapa->root) == NULL)
    {
      free (heapa);
      return heapb;
    }
  if ((b_root = heapb->root) == NULL)
    {
      free (heapb);
      return heapa;
    }

  /* Merge them to the next nodes on the opposite chain.  */
  a_root->left->right = b_root;
  b_root->left->right = a_root;
  temp = a_root->left;
  a_root->left = b_root->left;
  b_root->left = temp;
  heapa->nodes += heapb->nodes;

  /* And set the new minimum, if it's changed.  */
  if (fibheap_compare (heapa, heapb->min, heapa->min) < 0)
    heapa->min = heapb->min;

  free (heapb);
  return heapa;
}

/* Extract the data of the minimum node from HEAP.  */
void *
fibheap_extract_min (fibheap_t heap)
{
  fibnode_t z;
  void *ret = NULL;

  /* If we don't have a min set, it means we have no nodes.  */
  if (heap->min != NULL)
    {
      /* Otherwise, extract the min node, free the node, and return the
         node's data.  */
      z = fibheap_extr_min_node (heap);
      ret = z->data;
      free (z);
    }

  return ret;
}

/* Replace both the KEY and the DATA associated with NODE.  */
void *
fibheap_replace_key_data (fibheap_t heap, fibnode_t node,
                          fibheapkey_t key, void *data)
{
  void *odata;
  fibheapkey_t okey;
  fibnode_t y;

  /* If we wanted to, we could actually do a real increase by redeleting and
     inserting. However, this would require O (log n) time. So just bail out
     for now.  */
  if (fibheap_comp_data (heap, key, data, node) > 0)
    return NULL;

  odata = node->data;
  okey = node->key;
  node->data = data;
  node->key = key;
  y = node->parent;

  if (okey == key)
    return odata;

  /* These two compares are specifically <= 0 to make sure that in the case
     of equality, a node we replaced the data on, becomes the new min.  This
     is needed so that delete's call to extractmin gets the right node.  */
  if (y != NULL && fibheap_compare (heap, node, y) <= 0)
    {
      fibheap_cut (heap, node, y);
      fibheap_cascading_cut (heap, y);
    }

  if (fibheap_compare (heap, node, heap->min) <= 0)
    heap->min = node;

  return odata;
}

/* Replace the DATA associated with NODE.  */
void *
fibheap_replace_data (fibheap_t heap, fibnode_t node, void *data)
{
  return fibheap_replace_key_data (heap, node, node->key, data);
}

/* Replace the KEY associated with NODE.  */
fibheapkey_t
fibheap_replace_key (fibheap_t heap, fibnode_t node, fibheapkey_t key)
{
  int okey = node->key;
  fibheap_replace_key_data (heap, node, key, node->data);
  return okey;
}

/* Delete NODE from HEAP.  */
void *
fibheap_delete_node (fibheap_t heap, fibnode_t node)
{
  void *ret = node->data;

  /* To perform delete, we just make it the min key, and extract.  */
  fibheap_replace_key (heap, node, FIBHEAPKEY_MIN);
  fibheap_extract_min (heap);

  return ret;
}

/* Delete HEAP.  */
void
fibheap_delete (fibheap_t heap)
{
  while (heap->min != NULL)
    free (fibheap_extr_min_node (heap));

  free (heap);
}

/* Determine if HEAP is empty.  */
int
fibheap_empty (fibheap_t heap)
{
  return heap->nodes == 0;
}

/* Extract the minimum node of the heap.  */
static fibnode_t
fibheap_extr_min_node (fibheap_t heap)
{
  fibnode_t ret = heap->min;
  fibnode_t x, y, orig;

  /* Attach the child list of the minimum node to the root list of the heap.
     If there is no child list, we don't do squat.  */
  for (x = ret->child, orig = NULL; x != orig && x != NULL; x = y)
    {
      if (orig == NULL)
	orig = x;
      y = x->right;
      x->parent = NULL;
      fibheap_ins_root (heap, x);
    }

  /* Remove the old root.  */
  fibheap_rem_root (heap, ret);
  heap->nodes--;

  /* If we are left with no nodes, then the min is NULL.  */
  if (heap->nodes == 0)
    heap->min = NULL;
  else
    {
      /* Otherwise, consolidate to find new minimum, as well as do the reorg
         work that needs to be done.  */
      heap->min = ret->right;
      fibheap_consolidate (heap);
    }

  return ret;
}

/* Insert NODE into the root list of HEAP.  */
static void
fibheap_ins_root (fibheap_t heap, fibnode_t node)
{
  /* If the heap is currently empty, the new node becomes the singleton
     circular root list.  */
  if (heap->root == NULL)
    {
      heap->root = node;
      node->left = node;
      node->right = node;
      return;
    }

  /* Otherwise, insert it in the circular root list between the root
     and it's right node.  */
  fibnode_insert_after (heap->root, node);
}

/* Remove NODE from the rootlist of HEAP.  */
static void
fibheap_rem_root (fibheap_t heap, fibnode_t node)
{
  if (node->left == node)
    heap->root = NULL;
  else
    heap->root = fibnode_remove (node);
}

/* Consolidate the heap.  */
static void
fibheap_consolidate (fibheap_t heap)
{
  fibnode_t a[1 + 8 * sizeof (long)];
  fibnode_t w;
  fibnode_t y;
  fibnode_t x;
  int i;
  int d;
  int D;

  D = 1 + 8 * sizeof (long);

  memset (a, 0, sizeof (fibnode_t) * D);

  while ((w = heap->root) != NULL)
    {
      x = w;
      fibheap_rem_root (heap, w);
      d = x->degree;
      while (a[d] != NULL)
	{
	  y = a[d];
	  if (fibheap_compare (heap, x, y) > 0)
	    {
	      fibnode_t temp;
	      temp = x;
	      x = y;
	      y = temp;
	    }
	  fibheap_link (heap, y, x);
	  a[d] = NULL;
	  d++;
	}
      a[d] = x;
    }
  heap->min = NULL;
  for (i = 0; i < D; i++)
    if (a[i] != NULL)
      {
	fibheap_ins_root (heap, a[i]);
	if (heap->min == NULL || fibheap_compare (heap, a[i], heap->min) < 0)
	  heap->min = a[i];
      }
}

/* Make NODE a child of PARENT.  */
static void
fibheap_link (fibheap_t heap ATTRIBUTE_UNUSED,
              fibnode_t node, fibnode_t parent)
{
  if (parent->child == NULL)
    parent->child = node;
  else
    fibnode_insert_before (parent->child, node);
  node->parent = parent;
  parent->degree++;
  node->mark = 0;
}

/* Remove NODE from PARENT's child list.  */
static void
fibheap_cut (fibheap_t heap, fibnode_t node, fibnode_t parent)
{
  fibnode_remove (node);
  parent->degree--;
  fibheap_ins_root (heap, node);
  node->parent = NULL;
  node->mark = 0;
}

static void
fibheap_cascading_cut (fibheap_t heap, fibnode_t y)
{
  fibnode_t z;

  while ((z = y->parent) != NULL)
    {
      if (y->mark == 0)
	{
	  y->mark = 1;
	  return;
	}
      else
	{
	  fibheap_cut (heap, y, z);
	  y = z;
	}
    }
}

static void
fibnode_insert_after (fibnode_t a, fibnode_t b)
{
  if (a == a->right)
    {
      a->right = b;
      a->left = b;
      b->right = a;
      b->left = a;
    }
  else
    {
      b->right = a->right;
      a->right->left = b;
      a->right = b;
      b->left = a;
    }
}

static fibnode_t
fibnode_remove (fibnode_t node)
{
  fibnode_t ret;

  if (node == node->left)
    ret = NULL;
  else
    ret = node->left;

  if (node->parent != NULL && node->parent->child == node)
    node->parent->child = ret;

  node->right->left = node->left;
  node->left->right = node->right;

  node->parent = NULL;
  node->left = node;
  node->right = node;

  return ret;
}

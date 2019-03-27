/* A splay-tree datatype.  
   Copyright (C) 1998, 1999, 2000, 2001 Free Software Foundation, Inc.
   Contributed by Mark Mitchell (mark@markmitchell.com).

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

/* For an easily readable description of splay-trees, see:

     Lewis, Harry R. and Denenberg, Larry.  Data Structures and Their
     Algorithms.  Harper-Collins, Inc.  1991.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <stdio.h>

#include "libiberty.h"
#include "splay-tree.h"

static void splay_tree_delete_helper (splay_tree, splay_tree_node);
static inline void rotate_left (splay_tree_node *,
				splay_tree_node, splay_tree_node);
static inline void rotate_right (splay_tree_node *,
				splay_tree_node, splay_tree_node);
static void splay_tree_splay (splay_tree, splay_tree_key);
static int splay_tree_foreach_helper (splay_tree, splay_tree_node,
                                      splay_tree_foreach_fn, void*);

/* Deallocate NODE (a member of SP), and all its sub-trees.  */

static void 
splay_tree_delete_helper (splay_tree sp, splay_tree_node node)
{
  splay_tree_node pending = 0;
  splay_tree_node active = 0;

  if (!node)
    return;

#define KDEL(x)  if (sp->delete_key) (*sp->delete_key)(x);
#define VDEL(x)  if (sp->delete_value) (*sp->delete_value)(x);

  KDEL (node->key);
  VDEL (node->value);

  /* We use the "key" field to hold the "next" pointer.  */
  node->key = (splay_tree_key)pending;
  pending = (splay_tree_node)node;

  /* Now, keep processing the pending list until there aren't any
     more.  This is a little more complicated than just recursing, but
     it doesn't toast the stack for large trees.  */

  while (pending)
    {
      active = pending;
      pending = 0;
      while (active)
	{
	  splay_tree_node temp;

	  /* active points to a node which has its key and value
	     deallocated, we just need to process left and right.  */

	  if (active->left)
	    {
	      KDEL (active->left->key);
	      VDEL (active->left->value);
	      active->left->key = (splay_tree_key)pending;
	      pending = (splay_tree_node)(active->left);
	    }
	  if (active->right)
	    {
	      KDEL (active->right->key);
	      VDEL (active->right->value);
	      active->right->key = (splay_tree_key)pending;
	      pending = (splay_tree_node)(active->right);
	    }

	  temp = active;
	  active = (splay_tree_node)(temp->key);
	  (*sp->deallocate) ((char*) temp, sp->allocate_data);
	}
    }
#undef KDEL
#undef VDEL
}

/* Rotate the edge joining the left child N with its parent P.  PP is the
   grandparents pointer to P.  */

static inline void
rotate_left (splay_tree_node *pp, splay_tree_node p, splay_tree_node n)
{
  splay_tree_node tmp;
  tmp = n->right;
  n->right = p;
  p->left = tmp;
  *pp = n;
}

/* Rotate the edge joining the right child N with its parent P.  PP is the
   grandparents pointer to P.  */

static inline void
rotate_right (splay_tree_node *pp, splay_tree_node p, splay_tree_node n)
{
  splay_tree_node tmp;
  tmp = n->left;
  n->left = p;
  p->right = tmp;
  *pp = n;
}

/* Bottom up splay of key.  */

static void
splay_tree_splay (splay_tree sp, splay_tree_key key)
{
  if (sp->root == 0)
    return;

  do {
    int cmp1, cmp2;
    splay_tree_node n, c;

    n = sp->root;
    cmp1 = (*sp->comp) (key, n->key);

    /* Found.  */
    if (cmp1 == 0)
      return;

    /* Left or right?  If no child, then we're done.  */
    if (cmp1 < 0)
      c = n->left;
    else
      c = n->right;
    if (!c)
      return;

    /* Next one left or right?  If found or no child, we're done
       after one rotation.  */
    cmp2 = (*sp->comp) (key, c->key);
    if (cmp2 == 0
        || (cmp2 < 0 && !c->left)
        || (cmp2 > 0 && !c->right))
      {
	if (cmp1 < 0)
	  rotate_left (&sp->root, n, c);
	else
	  rotate_right (&sp->root, n, c);
        return;
      }

    /* Now we have the four cases of double-rotation.  */
    if (cmp1 < 0 && cmp2 < 0)
      {
	rotate_left (&n->left, c, c->left);
	rotate_left (&sp->root, n, n->left);
      }
    else if (cmp1 > 0 && cmp2 > 0)
      {
	rotate_right (&n->right, c, c->right);
	rotate_right (&sp->root, n, n->right);
      }
    else if (cmp1 < 0 && cmp2 > 0)
      {
	rotate_right (&n->left, c, c->right);
	rotate_left (&sp->root, n, n->left);
      }
    else if (cmp1 > 0 && cmp2 < 0)
      {
	rotate_left (&n->right, c, c->left);
	rotate_right (&sp->root, n, n->right);
      }
  } while (1);
}

/* Call FN, passing it the DATA, for every node below NODE, all of
   which are from SP, following an in-order traversal.  If FN every
   returns a non-zero value, the iteration ceases immediately, and the
   value is returned.  Otherwise, this function returns 0.  */

static int
splay_tree_foreach_helper (splay_tree sp, splay_tree_node node,
                           splay_tree_foreach_fn fn, void *data)
{
  int val;

  if (!node)
    return 0;

  val = splay_tree_foreach_helper (sp, node->left, fn, data);
  if (val)
    return val;

  val = (*fn)(node, data);
  if (val)
    return val;

  return splay_tree_foreach_helper (sp, node->right, fn, data);
}


/* An allocator and deallocator based on xmalloc.  */
static void *
splay_tree_xmalloc_allocate (int size, void *data ATTRIBUTE_UNUSED)
{
  return (void *) xmalloc (size);
}

static void
splay_tree_xmalloc_deallocate (void *object, void *data ATTRIBUTE_UNUSED)
{
  free (object);
}


/* Allocate a new splay tree, using COMPARE_FN to compare nodes,
   DELETE_KEY_FN to deallocate keys, and DELETE_VALUE_FN to deallocate
   values.  Use xmalloc to allocate the splay tree structure, and any
   nodes added.  */

splay_tree 
splay_tree_new (splay_tree_compare_fn compare_fn,
                splay_tree_delete_key_fn delete_key_fn,
                splay_tree_delete_value_fn delete_value_fn)
{
  return (splay_tree_new_with_allocator
          (compare_fn, delete_key_fn, delete_value_fn,
           splay_tree_xmalloc_allocate, splay_tree_xmalloc_deallocate, 0));
}


/* Allocate a new splay tree, using COMPARE_FN to compare nodes,
   DELETE_KEY_FN to deallocate keys, and DELETE_VALUE_FN to deallocate
   values.  */

splay_tree 
splay_tree_new_with_allocator (splay_tree_compare_fn compare_fn,
                               splay_tree_delete_key_fn delete_key_fn,
                               splay_tree_delete_value_fn delete_value_fn,
                               splay_tree_allocate_fn allocate_fn,
                               splay_tree_deallocate_fn deallocate_fn,
                               void *allocate_data)
{
  splay_tree sp = (splay_tree) (*allocate_fn) (sizeof (struct splay_tree_s),
                                               allocate_data);
  sp->root = 0;
  sp->comp = compare_fn;
  sp->delete_key = delete_key_fn;
  sp->delete_value = delete_value_fn;
  sp->allocate = allocate_fn;
  sp->deallocate = deallocate_fn;
  sp->allocate_data = allocate_data;

  return sp;
}

/* Deallocate SP.  */

void 
splay_tree_delete (splay_tree sp)
{
  splay_tree_delete_helper (sp, sp->root);
  (*sp->deallocate) ((char*) sp, sp->allocate_data);
}

/* Insert a new node (associating KEY with DATA) into SP.  If a
   previous node with the indicated KEY exists, its data is replaced
   with the new value.  Returns the new node.  */

splay_tree_node
splay_tree_insert (splay_tree sp, splay_tree_key key, splay_tree_value value)
{
  int comparison = 0;

  splay_tree_splay (sp, key);

  if (sp->root)
    comparison = (*sp->comp)(sp->root->key, key);

  if (sp->root && comparison == 0)
    {
      /* If the root of the tree already has the indicated KEY, just
	 replace the value with VALUE.  */
      if (sp->delete_value)
	(*sp->delete_value)(sp->root->value);
      sp->root->value = value;
    } 
  else 
    {
      /* Create a new node, and insert it at the root.  */
      splay_tree_node node;
      
      node = ((splay_tree_node)
              (*sp->allocate) (sizeof (struct splay_tree_node_s),
                               sp->allocate_data));
      node->key = key;
      node->value = value;
      
      if (!sp->root)
	node->left = node->right = 0;
      else if (comparison < 0)
	{
	  node->left = sp->root;
	  node->right = node->left->right;
	  node->left->right = 0;
	}
      else
	{
	  node->right = sp->root;
	  node->left = node->right->left;
	  node->right->left = 0;
	}

      sp->root = node;
    }

  return sp->root;
}

/* Remove KEY from SP.  It is not an error if it did not exist.  */

void
splay_tree_remove (splay_tree sp, splay_tree_key key)
{
  splay_tree_splay (sp, key);

  if (sp->root && (*sp->comp) (sp->root->key, key) == 0)
    {
      splay_tree_node left, right;

      left = sp->root->left;
      right = sp->root->right;

      /* Delete the root node itself.  */
      if (sp->delete_value)
	(*sp->delete_value) (sp->root->value);
      (*sp->deallocate) (sp->root, sp->allocate_data);

      /* One of the children is now the root.  Doesn't matter much
	 which, so long as we preserve the properties of the tree.  */
      if (left)
	{
	  sp->root = left;

	  /* If there was a right child as well, hang it off the 
	     right-most leaf of the left child.  */
	  if (right)
	    {
	      while (left->right)
		left = left->right;
	      left->right = right;
	    }
	}
      else
	sp->root = right;
    }
}

/* Lookup KEY in SP, returning VALUE if present, and NULL 
   otherwise.  */

splay_tree_node
splay_tree_lookup (splay_tree sp, splay_tree_key key)
{
  splay_tree_splay (sp, key);

  if (sp->root && (*sp->comp)(sp->root->key, key) == 0)
    return sp->root;
  else
    return 0;
}

/* Return the node in SP with the greatest key.  */

splay_tree_node
splay_tree_max (splay_tree sp)
{
  splay_tree_node n = sp->root;

  if (!n)
    return NULL;

  while (n->right)
    n = n->right;

  return n;
}

/* Return the node in SP with the smallest key.  */

splay_tree_node
splay_tree_min (splay_tree sp)
{
  splay_tree_node n = sp->root;

  if (!n)
    return NULL;

  while (n->left)
    n = n->left;

  return n;
}

/* Return the immediate predecessor KEY, or NULL if there is no
   predecessor.  KEY need not be present in the tree.  */

splay_tree_node
splay_tree_predecessor (splay_tree sp, splay_tree_key key)
{
  int comparison;
  splay_tree_node node;

  /* If the tree is empty, there is certainly no predecessor.  */
  if (!sp->root)
    return NULL;

  /* Splay the tree around KEY.  That will leave either the KEY
     itself, its predecessor, or its successor at the root.  */
  splay_tree_splay (sp, key);
  comparison = (*sp->comp)(sp->root->key, key);

  /* If the predecessor is at the root, just return it.  */
  if (comparison < 0)
    return sp->root;

  /* Otherwise, find the rightmost element of the left subtree.  */
  node = sp->root->left;
  if (node)
    while (node->right)
      node = node->right;

  return node;
}

/* Return the immediate successor KEY, or NULL if there is no
   successor.  KEY need not be present in the tree.  */

splay_tree_node
splay_tree_successor (splay_tree sp, splay_tree_key key)
{
  int comparison;
  splay_tree_node node;

  /* If the tree is empty, there is certainly no successor.  */
  if (!sp->root)
    return NULL;

  /* Splay the tree around KEY.  That will leave either the KEY
     itself, its predecessor, or its successor at the root.  */
  splay_tree_splay (sp, key);
  comparison = (*sp->comp)(sp->root->key, key);

  /* If the successor is at the root, just return it.  */
  if (comparison > 0)
    return sp->root;

  /* Otherwise, find the leftmost element of the right subtree.  */
  node = sp->root->right;
  if (node)
    while (node->left)
      node = node->left;

  return node;
}

/* Call FN, passing it the DATA, for every node in SP, following an
   in-order traversal.  If FN every returns a non-zero value, the
   iteration ceases immediately, and the value is returned.
   Otherwise, this function returns 0.  */

int
splay_tree_foreach (splay_tree sp, splay_tree_foreach_fn fn, void *data)
{
  return splay_tree_foreach_helper (sp, sp->root, fn, data);
}

/* Splay-tree comparison function, treating the keys as ints.  */

int
splay_tree_compare_ints (splay_tree_key k1, splay_tree_key k2)
{
  if ((int) k1 < (int) k2)
    return -1;
  else if ((int) k1 > (int) k2)
    return 1;
  else 
    return 0;
}

/* Splay-tree comparison function, treating the keys as pointers.  */

int
splay_tree_compare_pointers (splay_tree_key k1, splay_tree_key k2)
{
  if ((char*) k1 < (char*) k2)
    return -1;
  else if ((char*) k1 > (char*) k2)
    return 1;
  else 
    return 0;
}

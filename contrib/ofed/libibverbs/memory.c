/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2006 Cisco Systems, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <config.h>

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <inttypes.h>

#include "ibverbs.h"

struct ibv_mem_node {
	enum {
		IBV_RED,
		IBV_BLACK
	}			color;
	struct ibv_mem_node    *parent;
	struct ibv_mem_node    *left, *right;
	uintptr_t		start, end;
	int			refcnt;
};

static struct ibv_mem_node *mm_root;
static pthread_mutex_t mm_mutex = PTHREAD_MUTEX_INITIALIZER;
static int page_size;
static int huge_page_enabled;
static int too_late;

static unsigned long smaps_page_size(FILE *file)
{
	int n;
	unsigned long size = page_size;
	char buf[1024];

	while (fgets(buf, sizeof(buf), file) != NULL) {
		if (!strstr(buf, "KernelPageSize:"))
			continue;

		n = sscanf(buf, "%*s %lu", &size);
		if (n < 1)
			continue;

		/* page size is printed in Kb */
		size = size * 1024;

		break;
	}

	return size;
}

static unsigned long get_page_size(void *base)
{
	unsigned long ret = page_size;
	pid_t pid;
	FILE *file;
	char buf[1024];

	pid = getpid();
	snprintf(buf, sizeof(buf), "/proc/%d/smaps", pid);

	file = fopen(buf, "r" STREAM_CLOEXEC);
	if (!file)
		goto out;

	while (fgets(buf, sizeof(buf), file) != NULL) {
		int n;
		uintptr_t range_start, range_end;

		n = sscanf(buf, "%" SCNxPTR "-%" SCNxPTR, &range_start, &range_end);

		if (n < 2)
			continue;

		if ((uintptr_t) base >= range_start && (uintptr_t) base < range_end) {
			ret = smaps_page_size(file);
			break;
		}
	}

	fclose(file);

out:
	return ret;
}

int ibv_fork_init(void)
{
	void *tmp, *tmp_aligned;
	int ret;
	unsigned long size;

	if (getenv("RDMAV_HUGEPAGES_SAFE"))
		huge_page_enabled = 1;

	if (mm_root)
		return 0;

	if (too_late)
		return EINVAL;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size < 0)
		return errno;

	if (posix_memalign(&tmp, page_size, page_size))
		return ENOMEM;

	if (huge_page_enabled) {
		size = get_page_size(tmp);
		tmp_aligned = (void *) ((uintptr_t) tmp & ~(size - 1));
	} else {
		size = page_size;
		tmp_aligned = tmp;
	}

	ret = madvise(tmp_aligned, size, MADV_DONTFORK) ||
	      madvise(tmp_aligned, size, MADV_DOFORK);

	free(tmp);

	if (ret)
		return ENOSYS;

	mm_root = malloc(sizeof *mm_root);
	if (!mm_root)
		return ENOMEM;

	mm_root->parent = NULL;
	mm_root->left   = NULL;
	mm_root->right  = NULL;
	mm_root->color  = IBV_BLACK;
	mm_root->start  = 0;
	mm_root->end    = UINTPTR_MAX;
	mm_root->refcnt = 0;

	return 0;
}

static struct ibv_mem_node *__mm_prev(struct ibv_mem_node *node)
{
	if (node->left) {
		node = node->left;
		while (node->right)
			node = node->right;
	} else {
		while (node->parent && node == node->parent->left)
			node = node->parent;

		node = node->parent;
	}

	return node;
}

static struct ibv_mem_node *__mm_next(struct ibv_mem_node *node)
{
	if (node->right) {
		node = node->right;
		while (node->left)
			node = node->left;
	} else {
		while (node->parent && node == node->parent->right)
			node = node->parent;

		node = node->parent;
	}

	return node;
}

static void __mm_rotate_right(struct ibv_mem_node *node)
{
	struct ibv_mem_node *tmp;

	tmp = node->left;

	node->left = tmp->right;
	if (node->left)
		node->left->parent = node;

	if (node->parent) {
		if (node->parent->right == node)
			node->parent->right = tmp;
		else
			node->parent->left = tmp;
	} else
		mm_root = tmp;

	tmp->parent = node->parent;

	tmp->right = node;
	node->parent = tmp;
}

static void __mm_rotate_left(struct ibv_mem_node *node)
{
	struct ibv_mem_node *tmp;

	tmp = node->right;

	node->right = tmp->left;
	if (node->right)
		node->right->parent = node;

	if (node->parent) {
		if (node->parent->right == node)
			node->parent->right = tmp;
		else
			node->parent->left = tmp;
	} else
		mm_root = tmp;

	tmp->parent = node->parent;

	tmp->left = node;
	node->parent = tmp;
}

#if 0
static int verify(struct ibv_mem_node *node)
{
	int hl, hr;

	if (!node)
		return 1;

	hl = verify(node->left);
	hr = verify(node->left);

	if (!hl || !hr)
		return 0;
	if (hl != hr)
		return 0;

	if (node->color == IBV_RED) {
		if (node->left && node->left->color != IBV_BLACK)
			return 0;
		if (node->right && node->right->color != IBV_BLACK)
			return 0;
		return hl;
	}

	return hl + 1;
}
#endif

static void __mm_add_rebalance(struct ibv_mem_node *node)
{
	struct ibv_mem_node *parent, *gp, *uncle;

	while (node->parent && node->parent->color == IBV_RED) {
		parent = node->parent;
		gp     = node->parent->parent;

		if (parent == gp->left) {
			uncle = gp->right;

			if (uncle && uncle->color == IBV_RED) {
				parent->color = IBV_BLACK;
				uncle->color  = IBV_BLACK;
				gp->color     = IBV_RED;

				node = gp;
			} else {
				if (node == parent->right) {
					__mm_rotate_left(parent);
					node   = parent;
					parent = node->parent;
				}

				parent->color = IBV_BLACK;
				gp->color     = IBV_RED;

				__mm_rotate_right(gp);
			}
		} else {
			uncle = gp->left;

			if (uncle && uncle->color == IBV_RED) {
				parent->color = IBV_BLACK;
				uncle->color  = IBV_BLACK;
				gp->color     = IBV_RED;

				node = gp;
			} else {
				if (node == parent->left) {
					__mm_rotate_right(parent);
					node   = parent;
					parent = node->parent;
				}

				parent->color = IBV_BLACK;
				gp->color     = IBV_RED;

				__mm_rotate_left(gp);
			}
		}
	}

	mm_root->color = IBV_BLACK;
}

static void __mm_add(struct ibv_mem_node *new)
{
	struct ibv_mem_node *node, *parent = NULL;

	node = mm_root;
	while (node) {
		parent = node;
		if (node->start < new->start)
			node = node->right;
		else
			node = node->left;
	}

	if (parent->start < new->start)
		parent->right = new;
	else
		parent->left = new;

	new->parent = parent;
	new->left   = NULL;
	new->right  = NULL;

	new->color = IBV_RED;
	__mm_add_rebalance(new);
}

static void __mm_remove(struct ibv_mem_node *node)
{
	struct ibv_mem_node *child, *parent, *sib, *tmp;
	int nodecol;

	if (node->left && node->right) {
		tmp = node->left;
		while (tmp->right)
			tmp = tmp->right;

		nodecol    = tmp->color;
		child      = tmp->left;
		tmp->color = node->color;

		if (tmp->parent != node) {
			parent        = tmp->parent;
			parent->right = tmp->left;
			if (tmp->left)
				tmp->left->parent = parent;

			tmp->left   	   = node->left;
			node->left->parent = tmp;
		} else
			parent = tmp;

		tmp->right          = node->right;
		node->right->parent = tmp;

		tmp->parent = node->parent;
		if (node->parent) {
			if (node->parent->left == node)
				node->parent->left = tmp;
			else
				node->parent->right = tmp;
		} else
			mm_root = tmp;
	} else {
		nodecol = node->color;

		child  = node->left ? node->left : node->right;
		parent = node->parent;

		if (child)
			child->parent = parent;
		if (parent) {
			if (parent->left == node)
				parent->left = child;
			else
				parent->right = child;
		} else
			mm_root = child;
	}

	free(node);

	if (nodecol == IBV_RED)
		return;

	while ((!child || child->color == IBV_BLACK) && child != mm_root) {
		if (parent->left == child) {
			sib = parent->right;

			if (sib->color == IBV_RED) {
				parent->color = IBV_RED;
				sib->color    = IBV_BLACK;
				__mm_rotate_left(parent);
				sib = parent->right;
			}

			if ((!sib->left  || sib->left->color  == IBV_BLACK) &&
			    (!sib->right || sib->right->color == IBV_BLACK)) {
				sib->color = IBV_RED;
				child  = parent;
				parent = child->parent;
			} else {
				if (!sib->right || sib->right->color == IBV_BLACK) {
					if (sib->left)
						sib->left->color = IBV_BLACK;
					sib->color = IBV_RED;
					__mm_rotate_right(sib);
					sib = parent->right;
				}

				sib->color    = parent->color;
				parent->color = IBV_BLACK;
				if (sib->right)
					sib->right->color = IBV_BLACK;
				__mm_rotate_left(parent);
				child = mm_root;
				break;
			}
		} else {
			sib = parent->left;

			if (sib->color == IBV_RED) {
				parent->color = IBV_RED;
				sib->color    = IBV_BLACK;
				__mm_rotate_right(parent);
				sib = parent->left;
			}

			if ((!sib->left  || sib->left->color  == IBV_BLACK) &&
			    (!sib->right || sib->right->color == IBV_BLACK)) {
				sib->color = IBV_RED;
				child  = parent;
				parent = child->parent;
			} else {
				if (!sib->left || sib->left->color == IBV_BLACK) {
					if (sib->right)
						sib->right->color = IBV_BLACK;
					sib->color = IBV_RED;
					__mm_rotate_left(sib);
					sib = parent->left;
				}

				sib->color    = parent->color;
				parent->color = IBV_BLACK;
				if (sib->left)
					sib->left->color = IBV_BLACK;
				__mm_rotate_right(parent);
				child = mm_root;
				break;
			}
		}
	}

	if (child)
		child->color = IBV_BLACK;
}

static struct ibv_mem_node *__mm_find_start(uintptr_t start, uintptr_t end)
{
	struct ibv_mem_node *node = mm_root;

	while (node) {
		if (node->start <= start && node->end >= start)
			break;

		if (node->start < start)
			node = node->right;
		else
			node = node->left;
	}

	return node;
}

static struct ibv_mem_node *merge_ranges(struct ibv_mem_node *node,
					 struct ibv_mem_node *prev)
{
	prev->end = node->end;
	prev->refcnt = node->refcnt;
	__mm_remove(node);

	return prev;
}

static struct ibv_mem_node *split_range(struct ibv_mem_node *node,
					uintptr_t cut_line)
{
	struct ibv_mem_node *new_node = NULL;

	new_node = malloc(sizeof *new_node);
	if (!new_node)
		return NULL;
	new_node->start  = cut_line;
	new_node->end    = node->end;
	new_node->refcnt = node->refcnt;
	node->end  = cut_line - 1;
	__mm_add(new_node);

	return new_node;
}

static struct ibv_mem_node *get_start_node(uintptr_t start, uintptr_t end,
					   int inc)
{
	struct ibv_mem_node *node, *tmp = NULL;

	node = __mm_find_start(start, end);
	if (node->start < start)
		node = split_range(node, start);
	else {
		tmp = __mm_prev(node);
		if (tmp && tmp->refcnt == node->refcnt + inc)
			node = merge_ranges(node, tmp);
	}
	return node;
}

/*
 * This function is called if madvise() fails to undo merging/splitting
 * operations performed on the node.
 */
static struct ibv_mem_node *undo_node(struct ibv_mem_node *node,
				      uintptr_t start, int inc)
{
	struct ibv_mem_node *tmp = NULL;

	/*
	 * This condition can be true only if we merged this
	 * node with the previous one, so we need to split them.
	*/
	if (start > node->start) {
		tmp = split_range(node, start);
		if (tmp) {
			node->refcnt += inc;
			node = tmp;
		} else
			return NULL;
	}

	tmp  =  __mm_prev(node);
	if (tmp && tmp->refcnt == node->refcnt)
		node = merge_ranges(node, tmp);

	tmp  =  __mm_next(node);
	if (tmp && tmp->refcnt == node->refcnt)
		node = merge_ranges(tmp, node);

	return node;
}

static int ibv_madvise_range(void *base, size_t size, int advice)
{
	uintptr_t start, end;
	struct ibv_mem_node *node, *tmp;
	int inc;
	int rolling_back = 0;
	int ret = 0;
	unsigned long range_page_size;

	if (!size)
		return 0;

	if (huge_page_enabled)
		range_page_size = get_page_size(base);
	else
		range_page_size = page_size;

	start = (uintptr_t) base & ~(range_page_size - 1);
	end   = ((uintptr_t) (base + size + range_page_size - 1) &
		 ~(range_page_size - 1)) - 1;

	pthread_mutex_lock(&mm_mutex);
again:
	inc = advice == MADV_DONTFORK ? 1 : -1;

	node = get_start_node(start, end, inc);
	if (!node) {
		ret = -1;
		goto out;
	}

	while (node && node->start <= end) {
		if (node->end > end) {
			if (!split_range(node, end + 1)) {
				ret = -1;
				goto out;
			}
		}

		if ((inc == -1 && node->refcnt == 1) ||
		    (inc ==  1 && node->refcnt == 0)) {
			/*
			 * If this is the first time through the loop,
			 * and we merged this node with the previous
			 * one, then we only want to do the madvise()
			 * on start ... node->end (rather than
			 * starting at node->start).
			 *
			 * Otherwise we end up doing madvise() on
			 * bigger region than we're being asked to,
			 * and that may lead to a spurious failure.
			 */
			if (start > node->start)
				ret = madvise((void *) start, node->end - start + 1,
					      advice);
			else
				ret = madvise((void *) node->start,
					      node->end - node->start + 1,
					      advice);
			if (ret) {
				node = undo_node(node, start, inc);

				if (rolling_back || !node)
					goto out;

				/* madvise failed, roll back previous changes */
				rolling_back = 1;
				advice = advice == MADV_DONTFORK ?
					MADV_DOFORK : MADV_DONTFORK;
				tmp = __mm_prev(node);
				if (!tmp || start > tmp->end)
					goto out;
				end = tmp->end;
				goto again;
			}
		}

		node->refcnt += inc;
		node = __mm_next(node);
	}

	if (node) {
		tmp = __mm_prev(node);
		if (tmp && node->refcnt == tmp->refcnt)
			node = merge_ranges(node, tmp);
	}

out:
	if (rolling_back)
		ret = -1;

	pthread_mutex_unlock(&mm_mutex);

	return ret;
}

int ibv_dontfork_range(void *base, size_t size)
{
	if (mm_root)
		return ibv_madvise_range(base, size, MADV_DONTFORK);
	else {
		too_late = 1;
		return 0;
	}
}

int ibv_dofork_range(void *base, size_t size)
{
	if (mm_root)
		return ibv_madvise_range(base, size, MADV_DOFORK);
	else {
		too_late = 1;
		return 0;
	}
}

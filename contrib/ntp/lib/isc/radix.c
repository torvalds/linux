/*
 * Copyright (C) 2007-2009, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id$ */

/*
 * This source was adapted from MRT's RCS Ids:
 * Id: radix.c,v 1.10.2.1 1999/11/29 05:16:24 masaki Exp
 * Id: prefix.c,v 1.37.2.9 2000/03/10 02:53:19 labovit Exp
 */

#include <config.h>

#include <isc/mem.h>
#include <isc/types.h>
#include <isc/util.h>
#include <isc/radix.h>

static isc_result_t
_new_prefix(isc_mem_t *mctx, isc_prefix_t **target, int family,
	    void *dest, int bitlen);

static void
_deref_prefix(isc_mem_t *mctx, isc_prefix_t *prefix);

static isc_result_t
_ref_prefix(isc_mem_t *mctx, isc_prefix_t **target, isc_prefix_t *prefix);

static int
_comp_with_mask(void *addr, void *dest, u_int mask);

static void
_clear_radix(isc_radix_tree_t *radix, isc_radix_destroyfunc_t func);

static isc_result_t
_new_prefix(isc_mem_t *mctx, isc_prefix_t **target, int family, void *dest,
	    int bitlen)
{
	isc_prefix_t *prefix;

	REQUIRE(target != NULL);

	if (family != AF_INET6 && family != AF_INET && family != AF_UNSPEC)
		return (ISC_R_NOTIMPLEMENTED);

	prefix = isc_mem_get(mctx, sizeof(isc_prefix_t));
	if (prefix == NULL)
		return (ISC_R_NOMEMORY);

	if (family == AF_INET6) {
		prefix->bitlen = (bitlen >= 0) ? bitlen : 128;
		memcpy(&prefix->add.sin6, dest, 16);
	} else {
		/* AF_UNSPEC is "any" or "none"--treat it as AF_INET */
		prefix->bitlen = (bitlen >= 0) ? bitlen : 32;
		memcpy(&prefix->add.sin, dest, 4);
	}

	prefix->family = family;

	isc_refcount_init(&prefix->refcount, 1);

	*target = prefix;
	return (ISC_R_SUCCESS);
}

static void
_deref_prefix(isc_mem_t *mctx, isc_prefix_t *prefix) {
	int refs;

	if (prefix == NULL)
		return;

	isc_refcount_decrement(&prefix->refcount, &refs);

	if (refs <= 0) {
		isc_refcount_destroy(&prefix->refcount);
		isc_mem_put(mctx, prefix, sizeof(isc_prefix_t));
	}
}

static isc_result_t
_ref_prefix(isc_mem_t *mctx, isc_prefix_t **target, isc_prefix_t *prefix) {
	INSIST(prefix != NULL);
	INSIST((prefix->family == AF_INET && prefix->bitlen <= 32) ||
	       (prefix->family == AF_INET6 && prefix->bitlen <= 128) ||
	       (prefix->family == AF_UNSPEC && prefix->bitlen == 0));
	REQUIRE(target != NULL && *target == NULL);

	/*
	 * If this prefix is a static allocation, copy it into new memory.
	 * (Note, the refcount still has to be destroyed by the calling
	 * routine.)
	 */
	if (isc_refcount_current(&prefix->refcount) == 0) {
		isc_result_t ret;
		ret = _new_prefix(mctx, target, prefix->family,
				  &prefix->add, prefix->bitlen);
		return ret;
	}

	isc_refcount_increment(&prefix->refcount, NULL);

	*target = prefix;
	return (ISC_R_SUCCESS);
}

static int
_comp_with_mask(void *addr, void *dest, u_int mask) {

	/* Mask length of zero matches everything */
	if (mask == 0)
		return (1);

	if (memcmp(addr, dest, mask / 8) == 0) {
		int n = mask / 8;
		int m = ((~0) << (8 - (mask % 8)));

		if ((mask % 8) == 0 ||
		    (((u_char *)addr)[n] & m) == (((u_char *)dest)[n] & m))
			return (1);
	}
	return (0);
}

isc_result_t
isc_radix_create(isc_mem_t *mctx, isc_radix_tree_t **target, int maxbits) {
	isc_radix_tree_t *radix;

	REQUIRE(target != NULL && *target == NULL);

	radix = isc_mem_get(mctx, sizeof(isc_radix_tree_t));
	if (radix == NULL)
		return (ISC_R_NOMEMORY);

	radix->mctx = mctx;
	radix->maxbits = maxbits;
	radix->head = NULL;
	radix->num_active_node = 0;
	radix->num_added_node = 0;
	RUNTIME_CHECK(maxbits <= RADIX_MAXBITS); /* XXX */
	radix->magic = RADIX_TREE_MAGIC;
	*target = radix;
	return (ISC_R_SUCCESS);
}

/*
 * if func is supplied, it will be called as func(node->data)
 * before deleting the node
 */

static void
_clear_radix(isc_radix_tree_t *radix, isc_radix_destroyfunc_t func) {

	REQUIRE(radix != NULL);

	if (radix->head != NULL) {

		isc_radix_node_t *Xstack[RADIX_MAXBITS+1];
		isc_radix_node_t **Xsp = Xstack;
		isc_radix_node_t *Xrn = radix->head;

		while (Xrn != NULL) {
			isc_radix_node_t *l = Xrn->l;
			isc_radix_node_t *r = Xrn->r;

			if (Xrn->prefix != NULL) {
				_deref_prefix(radix->mctx, Xrn->prefix);
				if (func != NULL && (Xrn->data[0] != NULL ||
						     Xrn->data[1] != NULL))
					func(Xrn->data);
			} else {
				INSIST(Xrn->data[0] == NULL &&
				       Xrn->data[1] == NULL);
			}

			isc_mem_put(radix->mctx, Xrn, sizeof(*Xrn));
			radix->num_active_node--;

			if (l != NULL) {
				if (r != NULL) {
					*Xsp++ = r;
				}
				Xrn = l;
			} else if (r != NULL) {
				Xrn = r;
			} else if (Xsp != Xstack) {
				Xrn = *(--Xsp);
			} else {
				Xrn = NULL;
			}
		}
	}
	RUNTIME_CHECK(radix->num_active_node == 0);
}


void
isc_radix_destroy(isc_radix_tree_t *radix, isc_radix_destroyfunc_t func)
{
	REQUIRE(radix != NULL);
	_clear_radix(radix, func);
	isc_mem_put(radix->mctx, radix, sizeof(*radix));
}


/*
 * func will be called as func(node->prefix, node->data)
 */
void
isc_radix_process(isc_radix_tree_t *radix, isc_radix_processfunc_t func)
{
	isc_radix_node_t *node;

	REQUIRE(func != NULL);

	RADIX_WALK(radix->head, node) {
		func(node->prefix, node->data);
	} RADIX_WALK_END;
}


isc_result_t
isc_radix_search(isc_radix_tree_t *radix, isc_radix_node_t **target,
		 isc_prefix_t *prefix)
{
	isc_radix_node_t *node;
	isc_radix_node_t *stack[RADIX_MAXBITS + 1];
	u_char *addr;
	isc_uint32_t bitlen;
	int tfamily = -1;
	int cnt = 0;

	REQUIRE(radix != NULL);
	REQUIRE(prefix != NULL);
	REQUIRE(target != NULL && *target == NULL);
	RUNTIME_CHECK(prefix->bitlen <= radix->maxbits);

	*target = NULL;

	if (radix->head == NULL) {
		return (ISC_R_NOTFOUND);
	}

	node = radix->head;
	addr = isc_prefix_touchar(prefix);
	bitlen = prefix->bitlen;

	while (node->bit < bitlen) {
		if (node->prefix)
			stack[cnt++] = node;

		if (BIT_TEST(addr[node->bit >> 3], 0x80 >> (node->bit & 0x07)))
			node = node->r;
		else
			node = node->l;

		if (node == NULL)
			break;
	}

	if (node && node->prefix)
		stack[cnt++] = node;

	while (cnt-- > 0) {
		node = stack[cnt];

		if (_comp_with_mask(isc_prefix_tochar(node->prefix),
				    isc_prefix_tochar(prefix),
				    node->prefix->bitlen)) {
			if (node->node_num[ISC_IS6(prefix->family)] != -1 &&
				 ((*target == NULL) ||
				  (*target)->node_num[ISC_IS6(tfamily)] >
				   node->node_num[ISC_IS6(prefix->family)])) {
				*target = node;
				tfamily = prefix->family;
			}
		}
	}

	if (*target == NULL) {
		return (ISC_R_NOTFOUND);
	} else {
		return (ISC_R_SUCCESS);
	}
}

isc_result_t
isc_radix_insert(isc_radix_tree_t *radix, isc_radix_node_t **target,
		 isc_radix_node_t *source, isc_prefix_t *prefix)
{
	isc_radix_node_t *node, *new_node, *parent, *glue = NULL;
	u_char *addr, *test_addr;
	isc_uint32_t bitlen, fam, check_bit, differ_bit;
	isc_uint32_t i, j, r;
	isc_result_t result;

	REQUIRE(radix != NULL);
	REQUIRE(target != NULL && *target == NULL);
	REQUIRE(prefix != NULL || (source != NULL && source->prefix != NULL));
	RUNTIME_CHECK(prefix == NULL || prefix->bitlen <= radix->maxbits);

	if (prefix == NULL)
		prefix = source->prefix;

	INSIST(prefix != NULL);

	bitlen = prefix->bitlen;
	fam = prefix->family;

	if (radix->head == NULL) {
		node = isc_mem_get(radix->mctx, sizeof(isc_radix_node_t));
		if (node == NULL)
			return (ISC_R_NOMEMORY);
		node->bit = bitlen;
		node->node_num[0] = node->node_num[1] = -1;
		node->prefix = NULL;
		result = _ref_prefix(radix->mctx, &node->prefix, prefix);
		if (result != ISC_R_SUCCESS) {
			isc_mem_put(radix->mctx, node,
				    sizeof(isc_radix_node_t));
			return (result);
		}
		node->parent = NULL;
		node->l = node->r = NULL;
		if (source != NULL) {
			/*
			 * If source is non-NULL, then we're merging in a
			 * node from an existing radix tree.  To keep
			 * the node_num values consistent, the calling
			 * function will add the total number of nodes
			 * added to num_added_node at the end of
			 * the merge operation--we don't do it here.
			 */
			if (source->node_num[0] != -1)
				node->node_num[0] = radix->num_added_node +
						    source->node_num[0];
			if (source->node_num[1] != -1)
				node->node_num[1] = radix->num_added_node +
						    source->node_num[1];
			node->data[0] = source->data[0];
			node->data[1] = source->data[1];
		} else {
			if (fam == AF_UNSPEC) {
				/* "any" or "none" */
				node->node_num[0] = node->node_num[1] =
					++radix->num_added_node;
			} else {
				node->node_num[ISC_IS6(fam)] =
					++radix->num_added_node;
			}
			node->data[0] = NULL;
			node->data[1] = NULL;
		}
		radix->head = node;
		radix->num_active_node++;
		*target = node;
		return (ISC_R_SUCCESS);
	}

	addr = isc_prefix_touchar(prefix);
	node = radix->head;

	while (node->bit < bitlen || node->prefix == NULL) {
		if (node->bit < radix->maxbits &&
		    BIT_TEST(addr[node->bit >> 3], 0x80 >> (node->bit & 0x07)))
		{
			if (node->r == NULL)
				break;
			node = node->r;
		} else {
			if (node->l == NULL)
				break;
			node = node->l;
		}

		INSIST(node != NULL);
	}

	INSIST(node->prefix != NULL);

	test_addr = isc_prefix_touchar(node->prefix);
	/* Find the first bit different. */
	check_bit = (node->bit < bitlen) ? node->bit : bitlen;
	differ_bit = 0;
	for (i = 0; i*8 < check_bit; i++) {
		if ((r = (addr[i] ^ test_addr[i])) == 0) {
			differ_bit = (i + 1) * 8;
			continue;
		}
		/* I know the better way, but for now. */
		for (j = 0; j < 8; j++) {
			if (BIT_TEST (r, (0x80 >> j)))
				break;
		}
		/* Must be found. */
		INSIST(j < 8);
		differ_bit = i * 8 + j;
		break;
	}

	if (differ_bit > check_bit)
		differ_bit = check_bit;

	parent = node->parent;
	while (parent != NULL && parent->bit >= differ_bit) {
		node = parent;
		parent = node->parent;
	}

	if (differ_bit == bitlen && node->bit == bitlen) {
		if (node->prefix != NULL) {
			/* Set node_num only if it hasn't been set before */
			if (source != NULL) {
				/* Merging node */
				if (node->node_num[0] == -1 &&
				    source->node_num[0] != -1) {
					node->node_num[0] =
						radix->num_added_node +
						source->node_num[0];
					node->data[0] = source->data[0];
				}
				if (node->node_num[1] == -1 &&
				    source->node_num[0] != -1) {
					node->node_num[1] =
						radix->num_added_node +
						source->node_num[1];
					node->data[1] = source->data[1];
				}
			} else {
				if (fam == AF_UNSPEC) {
					/* "any" or "none" */
					int next = radix->num_added_node + 1;
					if (node->node_num[0] == -1) {
						node->node_num[0] = next;
						radix->num_added_node = next;
					}
					if (node->node_num[1] == -1) {
						node->node_num[1] = next;
						radix->num_added_node = next;
					}
				} else {
					if (node->node_num[ISC_IS6(fam)] == -1)
						node->node_num[ISC_IS6(fam)]
						   = ++radix->num_added_node;
				}
			}
			*target = node;
			return (ISC_R_SUCCESS);
		} else {
			result =
				_ref_prefix(radix->mctx, &node->prefix, prefix);
			if (result != ISC_R_SUCCESS)
				return (result);
		}
		INSIST(node->data[0] == NULL && node->node_num[0] == -1 &&
		       node->data[1] == NULL && node->node_num[1] == -1);
		if (source != NULL) {
			/* Merging node */
			if (source->node_num[0] != -1) {
				node->node_num[0] = radix->num_added_node +
						    source->node_num[0];
				node->data[0] = source->data[0];
			}
			if (source->node_num[1] != -1) {
				node->node_num[1] = radix->num_added_node +
						    source->node_num[1];
				node->data[1] = source->data[1];
			}
		} else {
			if (fam == AF_UNSPEC) {
				/* "any" or "none" */
				node->node_num[0] = node->node_num[1] =
					++radix->num_added_node;
			} else {
				node->node_num[ISC_IS6(fam)] =
					++radix->num_added_node;
			}
		}
		*target = node;
		return (ISC_R_SUCCESS);
	}

	new_node = isc_mem_get(radix->mctx, sizeof(isc_radix_node_t));
	if (new_node == NULL)
		return (ISC_R_NOMEMORY);
	if (node->bit != differ_bit && bitlen != differ_bit) {
		glue = isc_mem_get(radix->mctx, sizeof(isc_radix_node_t));
		if (glue == NULL) {
			isc_mem_put(radix->mctx, new_node,
				    sizeof(isc_radix_node_t));
			return (ISC_R_NOMEMORY);
		}
	}
	new_node->bit = bitlen;
	new_node->prefix = NULL;
	result = _ref_prefix(radix->mctx, &new_node->prefix, prefix);
	if (result != ISC_R_SUCCESS) {
		isc_mem_put(radix->mctx, new_node, sizeof(isc_radix_node_t));
		if (glue != NULL)
			isc_mem_put(radix->mctx, glue,
				    sizeof(isc_radix_node_t));
		return (result);
	}
	new_node->parent = NULL;
	new_node->l = new_node->r = NULL;
	new_node->node_num[0] = new_node->node_num[1] = -1;
	radix->num_active_node++;

	if (source != NULL) {
		/* Merging node */
		if (source->node_num[0] != -1)
			new_node->node_num[0] = radix->num_added_node +
						source->node_num[0];
		if (source->node_num[1] != -1)
			new_node->node_num[1] = radix->num_added_node +
						source->node_num[1];
		new_node->data[0] = source->data[0];
		new_node->data[1] = source->data[1];
	} else {
		if (fam == AF_UNSPEC) {
			/* "any" or "none" */
			new_node->node_num[0] = new_node->node_num[1] =
				++radix->num_added_node;
		} else {
			new_node->node_num[ISC_IS6(fam)] =
				++radix->num_added_node;
		}
		new_node->data[0] = NULL;
		new_node->data[1] = NULL;
	}

	if (node->bit == differ_bit) {
		INSIST(glue == NULL);
		new_node->parent = node;
		if (node->bit < radix->maxbits &&
		    BIT_TEST(addr[node->bit >> 3], 0x80 >> (node->bit & 0x07)))
		{
			INSIST(node->r == NULL);
			node->r = new_node;
		} else {
			INSIST(node->l == NULL);
			node->l = new_node;
		}
		*target = new_node;
		return (ISC_R_SUCCESS);
	}

	if (bitlen == differ_bit) {
		INSIST(glue == NULL);
		if (bitlen < radix->maxbits &&
		    BIT_TEST(test_addr[bitlen >> 3], 0x80 >> (bitlen & 0x07))) {
			new_node->r = node;
		} else {
			new_node->l = node;
		}
		new_node->parent = node->parent;
		if (node->parent == NULL) {
			INSIST(radix->head == node);
			radix->head = new_node;
		} else if (node->parent->r == node) {
			node->parent->r = new_node;
		} else {
			node->parent->l = new_node;
		}
		node->parent = new_node;
	} else {
		INSIST(glue != NULL);
		glue->bit = differ_bit;
		glue->prefix = NULL;
		glue->parent = node->parent;
		glue->data[0] = glue->data[1] = NULL;
		glue->node_num[0] = glue->node_num[1] = -1;
		radix->num_active_node++;
		if (differ_bit < radix->maxbits &&
		    BIT_TEST(addr[differ_bit>>3], 0x80 >> (differ_bit & 07))) {
			glue->r = new_node;
			glue->l = node;
		} else {
			glue->r = node;
			glue->l = new_node;
		}
		new_node->parent = glue;

		if (node->parent == NULL) {
			INSIST(radix->head == node);
			radix->head = glue;
		} else if (node->parent->r == node) {
			node->parent->r = glue;
		} else {
			node->parent->l = glue;
		}
		node->parent = glue;
	}

	*target = new_node;
	return (ISC_R_SUCCESS);
}

void
isc_radix_remove(isc_radix_tree_t *radix, isc_radix_node_t *node) {
	isc_radix_node_t *parent, *child;

	REQUIRE(radix != NULL);
	REQUIRE(node != NULL);

	if (node->r && node->l) {
		/*
		 * This might be a placeholder node -- have to check and
		 * make sure there is a prefix associated with it!
		 */
		if (node->prefix != NULL)
			_deref_prefix(radix->mctx, node->prefix);

		node->prefix = NULL;
		node->data[0] = node->data[1] = NULL;
		return;
	}

	if (node->r == NULL && node->l == NULL) {
		parent = node->parent;
		_deref_prefix(radix->mctx, node->prefix);
		isc_mem_put(radix->mctx, node, sizeof(*node));
		radix->num_active_node--;

		if (parent == NULL) {
			INSIST(radix->head == node);
			radix->head = NULL;
			return;
		}

		if (parent->r == node) {
			parent->r = NULL;
			child = parent->l;
		} else {
			INSIST(parent->l == node);
			parent->l = NULL;
			child = parent->r;
		}

		if (parent->prefix)
			return;

		/* We need to remove parent too. */

		if (parent->parent == NULL) {
			INSIST(radix->head == parent);
			radix->head = child;
		} else if (parent->parent->r == parent) {
			parent->parent->r = child;
		} else {
			INSIST(parent->parent->l == parent);
			parent->parent->l = child;
		}
		child->parent = parent->parent;
		isc_mem_put(radix->mctx, parent, sizeof(*parent));
		radix->num_active_node--;
		return;
	}

	if (node->r) {
		child = node->r;
	} else {
		INSIST(node->l != NULL);
		child = node->l;
	}
	parent = node->parent;
	child->parent = parent;

	_deref_prefix(radix->mctx, node->prefix);
	isc_mem_put(radix->mctx, node, sizeof(*node));
	radix->num_active_node--;

	if (parent == NULL) {
		INSIST(radix->head == node);
		radix->head = child;
		return;
	}

	if (parent->r == node) {
		parent->r = child;
	} else {
		INSIST(parent->l == node);
		parent->l = child;
	}
}

/*
Local Variables:
c-basic-offset: 4
indent-tabs-mode: t
End:
*/

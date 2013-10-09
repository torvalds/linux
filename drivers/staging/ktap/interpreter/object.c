/*
 * object.c - ktap object generic operation
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * Copyright (C) 1994-2013 Lua.org, PUC-Rio.
 *  - The part of code in this file is copied from lua initially.
 *  - lua's MIT license is compatible with GPL.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef __KERNEL__
#include "../include/ktap.h"
#else
#include "../include/ktap_types.h"
#endif

#ifdef __KERNEL__

#define KTAP_ALLOC_FLAGS ((GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN) \
			 & ~__GFP_WAIT)

void *kp_malloc(ktap_state *ks, int size)
{
	void *addr;

	/*
	 * Normally we don't want to trace under memory pressure,
	 * so we use a simple rule to handle memory allocation failure:
	 *
	 * retry until allocation success, this will make caller don't need
	 * to handle the unlikely failure case, then ktap exit.
	 *
	 * In this approach, if user find there have memory allocation failure,
	 * user should re-run the ktap script, or fix the memory pressure
	 * issue, or figure out why the script need so many memory.
	 *
	 * Perhaps return pre-allocated stub memory trunk when allocate failed
	 * is a better approch?
	 */
	addr = kmalloc(size, KTAP_ALLOC_FLAGS);
	if (unlikely(!addr)) {
		kp_error(ks, "kmalloc size %d failed, retry again\n", size);
		printk("ktap kmalloc size %d failed, retry again\n", size);
		dump_stack();
		while (1) {
			addr = kmalloc(size, KTAP_ALLOC_FLAGS);
			if (addr)
				break;
		}
		kp_printf(ks, "kmalloc retry success after failed, exit\n");
	}

	return addr;
}

void kp_free(ktap_state *ks, void *addr)
{
	kfree(addr);
}

void *kp_reallocv(ktap_state *ks, void *addr, int oldsize, int newsize)
{
	void *new_addr;

	new_addr = krealloc(addr, newsize, KTAP_ALLOC_FLAGS);
	if (unlikely(!new_addr)) {
		kp_error(ks, "krealloc size %d failed, retry again\n", newsize);
		printk("ktap krealloc size %d failed, retry again\n", newsize);
		dump_stack();
		while (1) {
			new_addr = krealloc(addr, newsize, KTAP_ALLOC_FLAGS);
			if (new_addr)
				break;
		}
		kp_printf(ks, "krealloc retry success after failed, exit\n");
	}

	return new_addr;
}

void *kp_zalloc(ktap_state *ks, int size)
{
	void *addr;

	addr = kzalloc(size, KTAP_ALLOC_FLAGS);
	if (unlikely(!addr)) {
		kp_error(ks, "kzalloc size %d failed, retry again\n", size);
		printk("ktap kzalloc size %d failed, retry again\n", size);
		dump_stack();
		while (1) {
			addr = kzalloc(size, KTAP_ALLOC_FLAGS);
			if (addr)
				break;
		}
		kp_printf(ks, "kzalloc retry success after failed, exit\n");
	}

	return addr;
}
#endif

void kp_obj_dump(ktap_state *ks, const ktap_value *v)
{
	switch (ttype(v)) {
	case KTAP_TNIL:
		kp_puts(ks, "NIL");
		break;
	case KTAP_TNUMBER:
		kp_printf(ks, "NUMBER %ld", nvalue(v));
		break;
	case KTAP_TBOOLEAN:
		kp_printf(ks, "BOOLEAN %d", bvalue(v));
		break;
	case KTAP_TLIGHTUSERDATA:
		kp_printf(ks, "LIGHTUSERDATA 0x%lx", (unsigned long)pvalue(v));
		break;
	case KTAP_TLCF:
		kp_printf(ks, "LIGHTCFCUNTION 0x%lx", (unsigned long)fvalue(v));
		break;
	case KTAP_TSHRSTR:
	case KTAP_TLNGSTR:
		kp_printf(ks, "SHRSTR #%s", svalue(v));
		break;
	case KTAP_TUSERDATA:
		kp_printf(ks, "USERDATA 0x%lx", (unsigned long)uvalue(v));
		break;
	case KTAP_TTABLE:
		kp_printf(ks, "TABLE 0x%lx", (unsigned long)hvalue(v));
		break;
        default:
		kp_printf(ks, "GCVALUE 0x%lx", (unsigned long)gcvalue(v));
		break;
	}
}

#ifdef __KERNEL__
#include <linux/stacktrace.h>
#include <linux/kallsyms.h>

static void kp_btrace_dump(ktap_state *ks, ktap_btrace *bt)
{
	char str[KSYM_SYMBOL_LEN];
	int i;

	for (i = 0; i < bt->nr_entries; i++) {
		unsigned long p = bt->entries[i];

		if (p == ULONG_MAX)
			break;

		SPRINT_SYMBOL(str, p);
		kp_printf(ks, "%s\n", str);
	}
}

static int kp_btrace_equal(ktap_btrace *bt1, ktap_btrace *bt2)
{
	int i;

	if (bt1->nr_entries != bt2->nr_entries)
		return 0;

	for (i = 0; i < bt1->nr_entries; i++) {
		if (bt1->entries[i] != bt2->entries[i])
			return 0;
	}

	return 1;
}
#endif

void kp_showobj(ktap_state *ks, const ktap_value *v)
{
	switch (ttype(v)) {
	case KTAP_TNIL:
		kp_puts(ks, "nil");
		break;
	case KTAP_TNUMBER:
		kp_printf(ks, "%ld", nvalue(v));
		break;
	case KTAP_TBOOLEAN:
		kp_puts(ks, (bvalue(v) == 1) ? "true" : "false");
		break;
	case KTAP_TLIGHTUSERDATA:
		kp_printf(ks, "0x%lx", (unsigned long)pvalue(v));
		break;
	case KTAP_TLCF:
		kp_printf(ks, "0x%lx", (unsigned long)fvalue(v));
		break;
	case KTAP_TSHRSTR:
	case KTAP_TLNGSTR:
		kp_puts(ks, svalue(v));
		break;
	case KTAP_TUSERDATA:
		kp_printf(ks, "0x%lx", (unsigned long)uvalue(v));
		break;
	case KTAP_TTABLE:
		kp_table_dump(ks, hvalue(v));
		break;
#ifdef __KERNEL__
	case KTAP_TEVENT:
		kp_transport_event_write(ks, evalue(v));
		break;
	case KTAP_TBTRACE:
		kp_btrace_dump(ks, btvalue(v));
		break;
	case KTAP_TAGGRTABLE:
		kp_aggrtable_dump(ks, ahvalue(v));
		break;
	case KTAP_TAGGRACCVAL:
		kp_aggraccval_dump(ks, aggraccvalue(v));
		break;
#endif
        default:
		kp_error(ks, "print unknown value type: %d\n", ttype(v));
		break;
	}
}


/*
 * equality of ktap values. ks == NULL means raw equality
 */
int kp_equalobjv(ktap_state *ks, const ktap_value *t1, const ktap_value *t2)
{
	switch (ttype(t1)) {
	case KTAP_TNIL:
		return 1;
	case KTAP_TNUMBER:
		return nvalue(t1) == nvalue(t2);
	case KTAP_TBOOLEAN:
		return bvalue(t1) == bvalue(t2);  /* true must be 1 !! */
	case KTAP_TLIGHTUSERDATA:
		return pvalue(t1) == pvalue(t2);
	case KTAP_TLCF:
		return fvalue(t1) == fvalue(t2);
	case KTAP_TSHRSTR:
		return eqshrstr(rawtsvalue(t1), rawtsvalue(t2));
	case KTAP_TLNGSTR:
		return kp_tstring_eqlngstr(rawtsvalue(t1), rawtsvalue(t2));
	case KTAP_TUSERDATA:
		if (uvalue(t1) == uvalue(t2))
			return 1;
		else if (ks == NULL)
			return 0;
	case KTAP_TTABLE:
		if (hvalue(t1) == hvalue(t2))
			return 1;
		else if (ks == NULL)
			return 0;
#ifdef __KERNEL__
	case KTAP_TBTRACE:
		return kp_btrace_equal(btvalue(t1), btvalue(t2));
#endif
	default:
		return gcvalue(t1) == gcvalue(t2);
	}

	return 0;
}

/*
 * ktap will not use lua's length operator on table meaning,
 * also # is not for length operator any more in ktap.
 *
 * Quote from lua mannal:
 * 2.5.5 - The Length Operator
 *
 * The length operator is denoted by the unary operator #.
 * The length of a string is its number of bytes(that is,
 * the usual meaning of string length when each character is one byte).
 *
 * The length of a table t is defined to be any integer index n
 * such that t[n] is not nil and t[n+1] is nil; moreover, if t[1] is nil,
 * n can be zero. For a regular array, with non-nil values from 1 to a given n,
 * its length is exactly that n, the index of its last value. If the array has
 * "holes" (that is, nil values between other non-nil values), then #t can be
 * any of the indices that directly precedes a nil value
 * (that is, it may consider any such nil value as the end of the array).
 */
int kp_objlen(ktap_state *ks, const ktap_value *v)
{
	switch(v->type) {
	case KTAP_TTABLE:
		return kp_table_length(ks, hvalue(v));
	case KTAP_TSTRING:
		return rawtsvalue(v)->tsv.len;
	default:
		kp_printf(ks, "cannot get length of type %d\n", v->type);
		return -1;
	}
	return 0;
}

/* need to protect allgc field? */
ktap_gcobject *kp_newobject(ktap_state *ks, int type, size_t size,
			    ktap_gcobject **list)
{
	ktap_gcobject *o;

	o = kp_malloc(ks, size);
	if (list == NULL)
		list = &G(ks)->allgc;

	gch(o)->tt = type;
	gch(o)->next = *list;
	*list = o;

	return o;
}

ktap_upval *kp_newupval(ktap_state *ks)
{
	ktap_upval *uv;

	uv = &kp_newobject(ks, KTAP_TUPVAL, sizeof(ktap_upval), NULL)->uv;
	uv->v = &uv->u.value;
	setnilvalue(uv->v);
	return uv;
}

static ktap_btrace *kp_newbacktrace(ktap_state *ks, ktap_gcobject **list)
{
	ktap_btrace *bt;

	bt = &kp_newobject(ks, KTAP_TBTRACE, sizeof(ktap_btrace), list)->bt;
	return bt;
}

void kp_objclone(ktap_state *ks, const ktap_value *o, ktap_value *newo,
		 ktap_gcobject **list)
{
	if (ttisbtrace(o)) {
		ktap_btrace *bt;
		bt = kp_newbacktrace(ks, list);
		bt->nr_entries = btvalue(o)->nr_entries;
		memcpy(&bt->entries[0], &btvalue(o)->entries[0],
					sizeof(bt->entries));
		setbtvalue(newo, bt);
	} else {
		kp_error(ks, "cannot clone ktap value type %d\n", ttype(o));
		setnilvalue(newo);
	}
}

ktap_closure *kp_newlclosure(ktap_state *ks, int n)
{
	ktap_closure *cl;

	cl = (ktap_closure *)kp_newobject(ks, KTAP_TLCL, sizeof(*cl), NULL);
	cl->l.p = NULL;
	cl->l.nupvalues = n;
	while (n--)
		cl->l.upvals[n] = NULL;

	return cl;
}

static void free_proto(ktap_state *ks, ktap_proto *f)
{
	kp_free(ks, f->code);
	kp_free(ks, f->p);
	kp_free(ks, f->k);
	kp_free(ks, f->lineinfo);
	kp_free(ks, f->locvars);
	kp_free(ks, f->upvalues);
	kp_free(ks, f);
}

ktap_proto *kp_newproto(ktap_state *ks)
{
	ktap_proto *f;
	f = (ktap_proto *)kp_newobject(ks, KTAP_TPROTO, sizeof(*f), NULL);
	f->k = NULL;
 	f->sizek = 0;
	f->p = NULL;
	f->sizep = 0;
	f->code = NULL;
	f->cache = NULL;
	f->sizecode = 0;
	f->lineinfo = NULL;
	f->sizelineinfo = 0;
	f->upvalues = NULL;
	f->sizeupvalues = 0;
	f->numparams = 0;
	f->is_vararg = 0;
	f->maxstacksize = 0;
	f->locvars = NULL;
	f->sizelocvars = 0;
	f->linedefined = 0;
	f->lastlinedefined = 0;
	f->source = NULL;
	return f;
}

static ktap_udata *newudata(ktap_state *ks, size_t s)
{
	ktap_udata *u;

	u = &kp_newobject(ks, KTAP_TUSERDATA, sizeof(ktap_udata) + s, NULL)->u;
	u->uv.len = s;
	return u;
}

void *kp_newuserdata(ktap_state *ks, size_t size)
{
	ktap_udata *u;

	u = newudata(ks, size);
	return u + 1;
}

void kp_free_gclist(ktap_state *ks, ktap_gcobject *o)
{
	while (o) {
		ktap_gcobject *next;

		next = gch(o)->next;
		switch (gch(o)->tt) {
		case KTAP_TTABLE:
			kp_table_free(ks, (ktap_table *)o);
			break;
		case KTAP_TPROTO:
			free_proto(ks, (ktap_proto *)o);
			break;
#ifdef __KERNEL__
		case KTAP_TAGGRTABLE:
			kp_aggrtable_free(ks, (ktap_aggrtable *)o);
			break;
#endif
		default:
			kp_free(ks, o);
		}
		o = next;
	}
}

void kp_free_all_gcobject(ktap_state *ks)
{
	kp_free_gclist(ks, G(ks)->allgc);
	G(ks)->allgc = NULL;
}

/******************************************************************************/

/*
 * make header for precompiled chunks
 * if you change the code below be sure to update load_header and FORMAT above
 * and KTAPC_HEADERSIZE in ktap_types.h
 */
void kp_header(u8 *h)
{
	int x = 1;

	memcpy(h, KTAP_SIGNATURE, sizeof(KTAP_SIGNATURE) - sizeof(char));
	h += sizeof(KTAP_SIGNATURE) - sizeof(char);
	*h++ = (u8)VERSION;
	*h++ = (u8)FORMAT;
	*h++ = (u8)(*(char*)&x);                    /* endianness */
	*h++ = (u8)(sizeof(int));
	*h++ = (u8)(sizeof(size_t));
	*h++ = (u8)(sizeof(ktap_instruction));
	*h++ = (u8)(sizeof(ktap_number));
	*h++ = (u8)(((ktap_number)0.5) == 0); /* is ktap_number integral? */
	memcpy(h, KTAPC_TAIL, sizeof(KTAPC_TAIL) - sizeof(char));
}



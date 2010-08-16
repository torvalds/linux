/*
 * Copyright (C) 1999 Gerhard Wichert, Siemens AG
 *                   Gerhard.Wichert@pdb.siemens.de
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Used in CONFIG_HIGHMEM systems for memory pages which
 * are not addressable by direct kernel virtual addresses.
 *
 */

#ifndef _ASM_TILE_HIGHMEM_H
#define _ASM_TILE_HIGHMEM_H

#include <linux/interrupt.h>
#include <linux/threads.h>
#include <asm/kmap_types.h>
#include <asm/tlbflush.h>
#include <asm/homecache.h>

/* declarations for highmem.c */
extern unsigned long highstart_pfn, highend_pfn;

extern pte_t *pkmap_page_table;

/*
 * Ordering is:
 *
 * FIXADDR_TOP
 *			fixed_addresses
 * FIXADDR_START
 *			temp fixed addresses
 * FIXADDR_BOOT_START
 *			Persistent kmap area
 * PKMAP_BASE
 * VMALLOC_END
 *			Vmalloc area
 * VMALLOC_START
 * high_memory
 */
#define LAST_PKMAP_MASK (LAST_PKMAP-1)
#define PKMAP_NR(virt)  ((virt-PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)  (PKMAP_BASE + ((nr) << PAGE_SHIFT))

void *kmap_high(struct page *page);
void kunmap_high(struct page *page);
void *kmap(struct page *page);
void kunmap(struct page *page);
void *kmap_fix_kpte(struct page *page, int finished);

/* This macro is used only in map_new_virtual() to map "page". */
#define kmap_prot page_to_kpgprot(page)

void kunmap_atomic_notypecheck(void *kvaddr, enum km_type type);
void *kmap_atomic_pfn(unsigned long pfn, enum km_type type);
void *kmap_atomic_prot_pfn(unsigned long pfn, enum km_type type, pgprot_t prot);
struct page *kmap_atomic_to_page(void *ptr);
void *kmap_atomic_prot(struct page *page, enum km_type type, pgprot_t prot);
void *kmap_atomic(struct page *page, enum km_type type);
void kmap_atomic_fix_kpte(struct page *page, int finished);

#define flush_cache_kmaps()	do { } while (0)

#endif /* _ASM_TILE_HIGHMEM_H */

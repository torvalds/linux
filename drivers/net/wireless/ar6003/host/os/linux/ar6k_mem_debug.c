//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------
//
#include "ar6000_drv.h"

#define malloc(size) kmalloc((size), GFP_ATOMIC)
#define free(ptr) kfree((ptr))

typedef struct MemInfo {
    void *ptr; 
    const char *caller;
    int lineno;
    size_t size;
} MemInfo;



typedef unsigned long hash_key;

typedef struct HashItem {
    hash_key Key;
    struct HashItem * Next;
} HashItem;

typedef struct a_hash {
    HashItem **Buckets;
    size_t len;
    int (*cmpKeyFunc)(const hash_key a, const hash_key b);
    unsigned long (*hashFunc)(const hash_key key);
    HashItem* (*mkNodeFunc)(hash_key key);
} a_hash;

static a_hash* gMem;

static int cmpMemInfo(const hash_key a, const hash_key b)
{
    return((const MemInfo*)a)->ptr - ((const MemInfo*)b)->ptr;
}

static unsigned long hashMemInfo(const hash_key key)
{
    return(unsigned long)((const MemInfo*)key)->ptr;
}

static HashItem* mkMemInfoItem(hash_key key)
{
    HashItem* item;
    item = malloc(sizeof(HashItem)+ sizeof(MemInfo));
    item->Key = (hash_key)(item+1);
    memcpy((void*)item->Key, (void*)key, sizeof(MemInfo));
    return item;
}

static void rmItem(HashItem* item, void* data)
{   /* private function, don't use !! */
    free(item);
}

HashItem* a_hash_add(a_hash *hash, const hash_key Key)
{
    int hashIndex = hash->hashFunc(Key) % hash->len;
    HashItem* item = hash->mkNodeFunc(Key);
    item->Next = hash->Buckets[hashIndex];
    hash->Buckets[hashIndex] = item;
    return item;
}

void a_hash_iterate(a_hash* hash, void (*func)(HashItem*, void*), void* data)
{
    size_t i;
    HashItem *p, *next;
    for (i=0; i<hash->len; ++i) {
        p = hash->Buckets[i];
        while (p) {
            next = p->Next;
            func(p, data);
            p = next;
        }
    }
}

void a_hash_clear(a_hash* hash)
{
    a_hash_iterate(hash, rmItem, NULL);
    memset(hash->Buckets, 0, hash->len * sizeof(HashItem*));
}

void a_hash_destroy(a_hash* hash)
{
    a_hash_clear(hash);
    free(hash);
}

static HashItem** __a_hash_find(a_hash* hash, const hash_key Key)
{
    int hashIndex = hash->hashFunc(Key) % hash->len;
    HashItem** result = &hash->Buckets[hashIndex];
    while (*result) {
        if (hash->cmpKeyFunc((*result)->Key, Key)==0)
            return result;
        else
            result = &(*result)->Next;
    }
    return result;
}

HashItem* a_hash_find(a_hash* hash, const hash_key Key)
{
    return *__a_hash_find(hash, Key);
}

HashItem* a_hash_findNext(a_hash* hash, HashItem* item)
{
    hash_key Key = (item) ? item->Key : 0;
    if (item)
        item = item->Next;
    while (item) {
        if (hash->cmpKeyFunc(item->Key, Key) == 0)
            return item;
        else
            item = item->Next;
    }
    return item;
}

void a_hash_erase(a_hash* hash, const hash_key Key)
{
    HashItem* p;
    HashItem** prev = __a_hash_find(hash, Key);
    p = *prev;
    if (p) {
        *prev = p->Next;
        rmItem(p, NULL);
    }
}

void __a_meminfo_add(void *ptr, size_t msize, const char *func, int lineno)
{
    MemInfo info;
    info.ptr = ptr;
    info.lineno = lineno;
    info.caller = func;
    info.size = msize;
    if (!gMem) {
        int len = 512;
        size_t size = sizeof(HashItem*) * len;
        gMem = (a_hash*)malloc(sizeof(a_hash)+size);
        memset(gMem, 0, sizeof(a_hash)+size);
        gMem->Buckets = (HashItem**)(gMem+1);
        gMem->len = len;

        gMem->mkNodeFunc = mkMemInfoItem;
        gMem->cmpKeyFunc = cmpMemInfo;
        gMem->hashFunc   = hashMemInfo;
    }

    a_hash_add(gMem, (hash_key)&info);
}

void a_meminfo_del(void *ptr)
{
    MemInfo info;
    info.ptr = ptr;
    if (gMem) {
        HashItem* p;
        HashItem** prev = __a_hash_find(gMem, (unsigned long)&info);
        p = *prev;
        if (p) {
            *prev = p->Next;
            rmItem(p, NULL);
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Warning! memory ptr %p not found!", __func__, ptr));
        }
    }
}

void* a_mem_alloc(size_t msize, int type, const char *func, int lineno)
{
    void *ptr = kmalloc(msize, type);
    __a_meminfo_add(ptr, msize, func, lineno);
    return ptr;
}

void a_mem_free(void *ptr)
{
    a_meminfo_del(ptr);
    kfree(ptr);   
}

static void printMemInfo(HashItem *item, void*arg)
{
    MemInfo *info = (MemInfo*)item->Key;
    int *total = (int*)arg;
    *total += info->size;
    A_PRINTF("%s line %d size %d ptr %p\n", info->caller, info->lineno, info->size, info->ptr);
}

void a_meminfo_report(int clear)
{
    int total = 0;
    A_PRINTF("AR6K Memory Report\n");
    if (gMem) {
        a_hash_iterate(gMem, printMemInfo, &total);
        if (clear) {
            a_hash_destroy(gMem);
        }
    }
    A_PRINTF("Total %d bytes\n", total);
}

A_BOOL a_meminfo_find(void *ptr)   
{
    MemInfo info;
    info.ptr = ptr;
    if (gMem) {
        HashItem* p;
        HashItem** prev = __a_hash_find(gMem, (unsigned long)&info);
        p = *prev;
        if (p) {
            return TRUE; 
        } else {
            return FALSE;
        }
    }
}

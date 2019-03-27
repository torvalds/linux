/*
 * Copyright (c) 2017 Mellanox Technologies LTD.  All rights reserved.
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
 *
 */

#if __cplusplus >= 201103L
#include <unordered_map>
#define	UM_NAMESPACE std
#else
#include <tr1/unordered_map>
#define	UM_NAMESPACE std::tr1
#endif

class HashTable {
public:
	UM_NAMESPACE::unordered_map<void *, void *> map;
	HashTable() { };
	~HashTable() { };
};

extern "C" {

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include "internal.h"

GHashTable *
GHashTableNew(void)
{
	return ((GHashTable *)(new HashTable()));
}

void
GHashTableDestroy(GHashTable *ght)
{
	delete (HashTable *)ght;
}

void
GHashTableInsert(GHashTable *ght, void *key, void *value)
{
	HashTable *ht = (HashTable *)ght;
	ht->map[key] = value;
}

void *
GHashTableLookup(GHashTable *ght, void *key)
{
	HashTable *ht = (HashTable *)ght;

	if (ht->map.find(key) == ht->map.end())
		return (NULL);
	return (ht->map[key]);
}

}

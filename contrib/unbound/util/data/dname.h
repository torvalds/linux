/*
 * util/data/dname.h - domain name routines
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains functions to deal with domain names (dnames).
 *
 * Some of the functions deal with domain names as a wireformat buffer,
 * with a length.
 */

#ifndef UTIL_DATA_DNAME_H
#define UTIL_DATA_DNAME_H
#include "util/storage/lruhash.h"
struct sldns_buffer;

/** max number of compression ptrs to follow */
#define MAX_COMPRESS_PTRS 256

/** 
 * Determine length of dname in buffer, no compression ptrs allowed, 
 * @param query: the ldns buffer, current position at start of dname.
 *	at end, position is at end of the dname.
 * @return: 0 on parse failure, or length including ending 0 of dname. 
 */
size_t query_dname_len(struct sldns_buffer* query);

/**
 * Determine if dname in memory is correct. no compression ptrs allowed.
 * @param dname: where dname starts in memory.
 * @param len: dname is not allowed to exceed this length (i.e. of allocation).
 * @return length of dname if dname is ok, 0 on a parse error.
 */
size_t dname_valid(uint8_t* dname, size_t len);

/** lowercase query dname */
void query_dname_tolower(uint8_t* dname);

/** 
 * lowercase pkt dname (follows compression pointers)
 * @param pkt: the packet, used to follow compression pointers. Position 
 *	is unchanged.
 * @param dname: start of dname in packet.
 */
void pkt_dname_tolower(struct sldns_buffer* pkt, uint8_t* dname);

/**
 * Compare query dnames (uncompressed storage). The Dnames passed do not
 * have to be lowercased, comparison routine does this.
 *
 * This routine is special, in that the comparison that it does corresponds
 * with the canonical comparison needed when comparing dnames inside rdata
 * for RR types that need canonicalization. That means that the first byte
 * that is smaller (possibly after lowercasing) makes an RR smaller, or the
 * shortest name makes an RR smaller.
 *
 * This routine does not compute the canonical order needed for NSEC 
 * processing.
 *
 * Dnames have to be valid format.
 * @param d1: dname to compare
 * @param d2: dname to compare
 * @return: -1, 0, or +1 depending on comparison results.
 * 	Sort order is first difference found. not the canonical ordering.
 */
int query_dname_compare(uint8_t* d1, uint8_t* d2);

/**
 * Determine correct, compressed, dname present in packet.
 * Checks for parse errors.
 * @param pkt: packet to read from (from current start position).
 * @return: 0 on parse error.
 *	At exit the position is right after the (compressed) dname.
 *	Compression pointers are followed and checked for loops.
 *	The uncompressed wireformat length is returned.
 */
size_t pkt_dname_len(struct sldns_buffer* pkt);

/**
 * Compare dnames in packet (compressed). Dnames must be valid.
 * routine performs lowercasing, so the packet casing is preserved.
 * @param pkt: packet, used to resolve compression pointers.
 * @param d1: dname to compare
 * @param d2: dname to compare
 * @return: -1, 0, or +1 depending on comparison results.
 * 	Sort order is first difference found. not the canonical ordering.
 */
int dname_pkt_compare(struct sldns_buffer* pkt, uint8_t* d1, uint8_t* d2);

/**
 * Hash dname, label by label, lowercasing, into hashvalue.
 * Dname in query format (not compressed).
 * @param dname: dname to hash.
 * @param h: initial hash value.
 * @return: result hash value.
 */
hashvalue_type dname_query_hash(uint8_t* dname, hashvalue_type h);

/**
 * Hash dname, label by label, lowercasing, into hashvalue.
 * Dname in pkt format (compressed).
 * @param pkt: packet, for resolving compression pointers.
 * @param dname: dname to hash, pointer to the pkt buffer.
 * 	Must be valid format. No loops, etc.
 * @param h: initial hash value.
 * @return: result hash value.
 * 	Result is the same as dname_query_hash, even if compression is used.
 */
hashvalue_type dname_pkt_hash(struct sldns_buffer* pkt, uint8_t* dname,
	hashvalue_type h);

/**
 * Copy over a valid dname and decompress it.
 * @param pkt: packet to resolve compression pointers.
 * @param to: buffer of size from pkt_len function to hold result.
 * @param dname: pointer into packet where dname starts.
 */
void dname_pkt_copy(struct sldns_buffer* pkt, uint8_t* to, uint8_t* dname);

/**
 * Copy over a valid dname to a packet.
 * @param pkt: packet to copy to.
 * @param dname: dname to copy.
 * @return: 0 if not enough space in buffer.
 */
int dname_buffer_write(struct sldns_buffer* pkt, uint8_t* dname);

/**
 * Count the number of labels in an uncompressed dname in memory.
 * @param dname: pointer to uncompressed dname.
 * @return: count of labels, including root label, "com." has 2 labels.
 */
int dname_count_labels(uint8_t* dname);

/**
 * Count labels and dname length both, for uncompressed dname in memory.
 * @param dname: pointer to uncompressed dname.
 * @param size: length of dname, including root label.
 * @return: count of labels, including root label, "com." has 2 labels.
 */
int dname_count_size_labels(uint8_t* dname, size_t* size);

/**
 * Compare dnames, sorted not canonical, but by label.
 * Such that zone contents follows zone apex.
 * @param d1: first dname. pointer to uncompressed wireformat.
 * @param labs1: number of labels in first dname.
 * @param d2: second dname. pointer to uncompressed wireformat.
 * @param labs2: number of labels in second dname.
 * @param mlabs: number of labels that matched exactly (the shared topdomain).
 * @return: 0 for equal, -1 smaller, or +1 d1 larger than d2.
 */
int dname_lab_cmp(uint8_t* d1, int labs1, uint8_t* d2, int labs2, int* mlabs);

/**
 * Check if labels starts with given prefix 
 * @param label: dname label
 * @param prefix: the string to match label with, null terminated.
 * @param endptr: pointer to location in label after prefix, only if return
 * 	value is 1. NULL if nothing in the label after the prefix, i.e. prefix
 * 	and label are the same.
 * @return: 1 if label starts with prefix, else 0
 */
int dname_lab_startswith(uint8_t* label, char* prefix, char** endptr);

/**
 * See if domain name d1 is a strict subdomain of d2.
 * That is a subdomain, but not equal. 
 * @param d1: domain name, uncompressed wireformat
 * @param labs1: number of labels in d1, including root label.
 * @param d2: domain name, uncompressed wireformat
 * @param labs2: number of labels in d2, including root label.
 * @return true if d1 is a subdomain of d2, but not equal to d2.
 */
int dname_strict_subdomain(uint8_t* d1, int labs1, uint8_t* d2, int labs2);

/**
 * Like dname_strict_subdomain but counts labels 
 * @param d1: domain name, uncompressed wireformat
 * @param d2: domain name, uncompressed wireformat
 * @return true if d1 is a subdomain of d2, but not equal to d2.
 */
int dname_strict_subdomain_c(uint8_t* d1, uint8_t* d2);

/**
 * Counts labels. Tests is d1 is a subdomain of d2.
 * @param d1: domain name, uncompressed wireformat
 * @param d2: domain name, uncompressed wireformat
 * @return true if d1 is a subdomain of d2.
 */
int dname_subdomain_c(uint8_t* d1, uint8_t* d2);

/** 
 * Debug helper. Print wireformat dname to output. 
 * @param out: like stdout or a file.
 * @param pkt: if not NULL, the packet for resolving compression ptrs.
 * @param dname: pointer to (start of) dname.
 */
void dname_print(FILE* out, struct sldns_buffer* pkt, uint8_t* dname);

/** 
 * Debug helper. Print dname to given string buffer (string buffer must
 * be at least 255 chars + 1 for the 0, in printable form.
 * This may lose information (? for nonprintable characters, or & if
 * the name is too long, # for a bad label length).
 * @param dname: uncompressed wireformat.
 * @param str: buffer of 255+1 length.
 */
void dname_str(uint8_t* dname, char* str);

/**
 * Returns true if the uncompressed wireformat dname is the root "."
 * @param dname: the dname to check
 * @return true if ".", false if not.
 */
int dname_is_root(uint8_t* dname);

/**
 * Snip off first label from a dname, returning the parent zone.
 * @param dname: from what to strip off. uncompressed wireformat.
 * @param len: length, adjusted to become less.
 * @return stripped off, or "." if input was ".".
 */
void dname_remove_label(uint8_t** dname, size_t* len);

/**
 * Snip off first N labels from a dname, returning the parent zone.
 * @param dname: from what to strip off. uncompressed wireformat.
 * @param len: length, adjusted to become less.
 * @param n: number of labels to strip off (from the left).
 * 	if 0, nothing happens.
 * @return stripped off, or "." if input was ".".
 */
void dname_remove_labels(uint8_t** dname, size_t* len, int n);

/**
 * Count labels for the RRSIG signature label field.
 * Like a normal labelcount, but "*" wildcard and "." root are not counted.
 * @param dname: valid uncompressed wireformat.
 * @return number of labels like in RRSIG; '*' and '.' are not counted.
 */
int dname_signame_label_count(uint8_t* dname);

/**
 * Return true if the label is a wildcard, *.example.com.
 * @param dname: valid uncompressed wireformat.
 * @return true if wildcard, or false.
 */
int dname_is_wild(uint8_t* dname);

/**
 * Compare dnames, Canonical in rfc4034 sense, but by label.
 * Such that zone contents follows zone apex.
 *
 * @param d1: first dname. pointer to uncompressed wireformat.
 * @param labs1: number of labels in first dname.
 * @param d2: second dname. pointer to uncompressed wireformat.
 * @param labs2: number of labels in second dname.
 * @param mlabs: number of labels that matched exactly (the shared topdomain).
 * @return: 0 for equal, -1 smaller, or +1 d1 larger than d2.
 */
int dname_canon_lab_cmp(uint8_t* d1, int labs1, uint8_t* d2, int labs2, 
	int* mlabs);

/**
 * Canonical dname compare. Takes care of counting labels.
 * Per rfc 4034 canonical order.
 *
 * @param d1: first dname. pointer to uncompressed wireformat.
 * @param d2: second dname. pointer to uncompressed wireformat.
 * @return: 0 for equal, -1 smaller, or +1 d1 larger than d2.
 */
int dname_canonical_compare(uint8_t* d1, uint8_t* d2);

/**
 * Get the shared topdomain between two names. Root "." or longer.
 * @param d1: first dname. pointer to uncompressed wireformat.
 * @param d2: second dname. pointer to uncompressed wireformat.
 * @return pointer to shared topdomain. Ptr to a part of d1.
 */
uint8_t* dname_get_shared_topdomain(uint8_t* d1, uint8_t* d2);

#endif /* UTIL_DATA_DNAME_H */

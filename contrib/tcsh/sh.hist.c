/* $Header: /p/tcsh/cvsroot/tcsh/sh.hist.c,v 3.61 2015/06/06 21:19:08 christos Exp $ */
/*
 * sh.hist.c: Shell history expansions and substitutions
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "sh.h"

RCSID("$tcsh: sh.hist.c,v 3.61 2015/06/06 21:19:08 christos Exp $")

#include <stdio.h>	/* for rename(2), grr. */
#include <assert.h>
#include "tc.h"
#include "dotlock.h"

extern int histvalid;
extern struct Strbuf histline;
Char HistLit = 0;

static	int	heq	(const struct wordent *, const struct wordent *);
static	void	hfree	(struct Hist *);

#define HIST_ONLY	0x01
#define HIST_SAVE	0x02
#define HIST_LOAD	0x04
#define HIST_REV	0x08
#define HIST_CLEAR	0x10
#define HIST_MERGE	0x20
#define HIST_TIME	0x40

/*
 * C shell
 */

/* Static functions don't show up in gprof summaries.  So eliminate "static"
 * modifier from some frequently called functions. */
#ifdef PROF
#define PG_STATIC
#else
#define PG_STATIC static
#endif

/* #define DEBUG_HIST 1 */

static const int fastMergeErase = 1;
static unsigned histCount = 0;		/* number elements on history list */
static int histlen = 0;
static struct Hist *histTail = NULL;     /* last element on history list */
static struct Hist *histMerg = NULL;	 /* last element merged by Htime */

static void insertHistHashTable(struct Hist *, unsigned);

/* Insert new element (hp) in history list after specified predecessor (pp). */
static void
hinsert(struct Hist *hp, struct Hist *pp)
{
    struct Hist *fp = pp->Hnext;        /* following element, if any */
    hp->Hnext = fp, hp->Hprev = pp;
    pp->Hnext = hp;
    if (fp)
        fp->Hprev = hp;
    else
        histTail = hp;                  /* meaning hp->Hnext == NULL */
    histCount++;
}

/* Remove the entry from the history list. */
static void
hremove(struct Hist *hp)
{
    struct Hist *pp = hp->Hprev;
    assert(pp);                         /* elements always have a previous */
    pp->Hnext = hp->Hnext;
    if (hp->Hnext)
        hp->Hnext->Hprev = pp;
    else
        histTail = pp;                  /* we must have been last */
    if (hp == histMerg)			/* deleting this hint from list */
	histMerg = NULL;
    assert(histCount > 0);
    histCount--;
}

/* Prune length of history list to specified size by history variable. */
PG_STATIC void
discardExcess(int hlen)
{
    struct Hist *hp, *np;
    if (histTail == NULL) {
        assert(histCount == 0);
        return;                         /* no entries on history list */
    }
    /* Prune dummy entries from the front, then old entries from the back. If
     * the list is still too long scan the whole list as before.  But only do a
     * full scan if the list is more than 6% (1/16th) too long. */
    while (histCount > (unsigned)hlen && (np = Histlist.Hnext)) {
        if (eventno - np->Href >= hlen || hlen == 0)
            hremove(np), hfree(np);
        else
            break;
    }
    while (histCount > (unsigned)hlen && (np = histTail) != &Histlist) {
        if (eventno - np->Href >= hlen || hlen == 0)
            hremove(np), hfree(np);
        else
            break;
    }
    if (histCount - (hlen >> 4) <= (unsigned)hlen)
	return;				/* don't bother doing the full scan */
    for (hp = &Histlist; histCount > (unsigned)hlen &&
	(np = hp->Hnext) != NULL;)
        if (eventno - np->Href >= hlen || hlen == 0)
            hremove(np), hfree(np);
        else
            hp = np;
}

/* Add the command "sp" to the history list. */
void
savehist(
  struct wordent *sp,
  int mflg)				/* true if -m (merge) specified */
{
    /* throw away null lines */
    if (sp && sp->next->word[0] == '\n')
	return;
    if (sp)
        (void) enthist(++eventno, sp, 1, mflg, histlen);
    discardExcess(histlen);
}

#define USE_JENKINS_HASH 1
/* #define USE_ONE_AT_A_TIME 1 */
#undef PRIME_LENGTH			/* no need for good HTL */

#ifdef USE_JENKINS_HASH
#define hashFcnName "lookup3"
/* From:
   lookup3.c, by Bob Jenkins, May 2006, Public Domain.
   "...  You can use this free for any purpose.  It's in
    the public domain.  It has no warranty."
   http://burtleburtle.net/bob/hash/index.html
 */

#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))
#define mix(a,b,c) \
{ \
  a -= c;  a ^= rot(c, 4);  c += b; \
  b -= a;  b ^= rot(a, 6);  a += c; \
  c -= b;  c ^= rot(b, 8);  b += a; \
  a -= c;  a ^= rot(c,16);  c += b; \
  b -= a;  b ^= rot(a,19);  a += c; \
  c -= b;  c ^= rot(b, 4);  b += a; \
}
#define final(a,b,c) \
{ \
  c ^= b; c -= rot(b,14); \
  a ^= c; a -= rot(c,11); \
  b ^= a; b -= rot(a,25); \
  c ^= b; c -= rot(b,16); \
  a ^= c; a -= rot(c, 4); \
  b ^= a; b -= rot(a,14); \
  c ^= b; c -= rot(b,24); \
}

struct hashValue		  /* State used to hash a wordend word list. */
{
    uint32_t a, b, c;
};

/* Set up the internal state */
static void
initializeHash(struct hashValue *h)
{
    h->a = h->b = h->c = 0xdeadbeef;
}

/* This does a partial hash of the Chars in a single word.  For efficiency we
 * include 3 versions of the code to pack Chars into 32-bit words for the
 * mixing function. */
static void
addWordToHash(struct hashValue *h, const Char *word)
{
    uint32_t a = h->a, b = h->b, c = h->c;
#ifdef SHORT_STRINGS
#ifdef WIDE_STRINGS
    assert(sizeof(Char) >= 4);
    while (1) {
	unsigned k;
	if ((k = (uChar)*word++) == 0) break; a += k;
	if ((k = (uChar)*word++) == 0) break; b += k;
	if ((k = (uChar)*word++) == 0) break; c += k;
	mix(a, b, c);
    }
#else
    assert(sizeof(Char) == 2);
    while (1) {
	unsigned k;
	if ((k = (uChar)*word++) == 0) break; a += k;
	if ((k = (uChar)*word++) == 0) break; a += k << 16;
	if ((k = (uChar)*word++) == 0) break; b += k;
	if ((k = (uChar)*word++) == 0) break; b += k << 16;
	if ((k = (uChar)*word++) == 0) break; c += k;
	if ((k = (uChar)*word++) == 0) break; c += k << 16;
	mix(a, b, c);
    }
#endif
#else
    assert(sizeof(Char) == 1);
    while (1) {
	unsigned k;
	if ((k = *word++) == 0) break; a += k;
	if ((k = *word++) == 0) break; a += k << 8;
	if ((k = *word++) == 0) break; a += k << 16;
	if ((k = *word++) == 0) break; a += k << 24;
	if ((k = *word++) == 0) break; b += k;
	if ((k = *word++) == 0) break; b += k << 8;
	if ((k = *word++) == 0) break; b += k << 16;
	if ((k = *word++) == 0) break; b += k << 24;
	if ((k = *word++) == 0) break; c += k;
	if ((k = *word++) == 0) break; c += k << 8;
	if ((k = *word++) == 0) break; c += k << 16;
	if ((k = *word++) == 0) break; c += k << 24;
	mix(a, b, c);
    }
#endif
    h->a = a, h->b = b, h->c = c;
}

static void
addCharToHash(struct hashValue *h, Char ch)
{
    /* The compiler (gcc -O2) seems to do a good job optimizing this without
     * explicitly extracting into local variables. */
    h->a += (uChar)ch;
    mix(h->a, h->b, h->c);
}

static uint32_t
finalizeHash(struct hashValue *h)
{
    uint32_t a = h->a, b = h->b, c = h->c;
    final(a, b, c);
    return c;
}

#elif USE_ONE_AT_A_TIME
#define hashFcnName "one-at-a-time"
/* This one is also from Bob Jenkins, but is slower but simpler than lookup3.
   "...  The code given here are all public domain."
   http://burtleburtle.net/bob/hash/doobs.html */

#if 0
ub4
one_at_a_time(char *key, ub4 len)
{
  ub4   hash, i;
  for (hash=0, i=0; i<len; ++i)
  {
    hash += key[i];
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }
  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return (hash & mask);
}
#endif

struct hashValue { uint32_t h; };
static void
initializeHash(struct hashValue *h)
{
    h->h = 0;
}

static void
addWordToHash(struct hashValue *h, const Char *word)
{
    unsigned k;
    uint32_t hash = h->h;
    while (k = (uChar)*word++)
	hash += k, hash += hash << 10, hash ^= hash >> 6;
    h->h = hash;
}

static void
addCharToHash(struct hashValue *h, Char c)
{
    Char b[2] = { c, 0 };
    addWordToHash(h, b);
}

static uint32_t
finalizeHash(struct hashValue *h)
{
    unsigned hash = h->h;
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

#else
#define hashFcnName "add-mul"
/* Simple multipy and add hash. */
#define PRIME_LENGTH 1			/* need "good" HTL */
struct hashValue { uint32_t h; };
static void
initializeHash(struct hashValue *h)
{
    h->h = 0xe13e2345;
}

static void
addWordToHash(struct hashValue *h, const Char *word)
{
    unsigned k;
    uint32_t hash = h->h;
    while (k = (uChar)*word++)
	hash = hash * 0x9e4167b9 + k;
    h->h = hash;
}

static void
addCharToHash(struct hashValue *h, Char c)
{
    h->h = h->h * 0x9e4167b9 + (uChar)c;
}

static uint32_t
finalizeHash(struct hashValue *h)
{
    return h->h;
}
#endif

static unsigned
hashhist(struct wordent *h0)
{
    struct hashValue s;
    struct wordent *firstWord = h0->next;
    struct wordent *h = firstWord;
    unsigned hash = 0;

    initializeHash(&s);
    for (; h != h0; h = h->next) {
        if (h->word[0] == '\n')
            break;                      /* don't hash newline */
        if (h != firstWord)
            addCharToHash(&s, ' ');	/* space between words */
	addWordToHash(&s, h->word);
    }
    hash = finalizeHash(&s);
    /* Zero means no hash value, so never return zero as a hash value. */
    return hash ? hash : 0x7fffffff;	/* prime! */
}

#if 0
unsigned
hashStr(Char *str)
{
    struct hashValue s;
    initializeHash(&s);
    addWordToHash(&s, str);
    return finalizeHash(&s);
}
#endif

#ifdef PRIME_LENGTH			/* need good HTL */
#define hash2tableIndex(hash, len) ((hash) % len)
#else
#define hash2tableIndex(hash, len) ((hash) & (len-1))
#endif

/* This code can be enabled to test the above hash functions for speed and
 * collision avoidance.  The testing is enabled by "occasional" calls to
 * displayHistStats(), see which. */
#ifdef DEBUG_HIST

#ifdef BSDTIMES
static double
doTiming(int start) {
    static struct timeval beginTime;
    if (start) {
	gettimeofday(&beginTime, NULL);
	return 0.0;
    } else {
	struct timeval now;
	gettimeofday(&now, NULL);
	return (now.tv_sec-beginTime.tv_sec) +
	    (now.tv_usec-beginTime.tv_usec)/1e6;
    }
}
#else
static double
doTiming(int start) {
    USE(start);
    return 0.0;
}
#endif

static void
generateHashes(int nChars, unsigned nWords, unsigned samples, unsigned *hashes,
    unsigned length)
{
    if (nChars < 1)
	return;
    nWords = (nWords < 1) ? 1 : (nWords > 4) ? 4 : nWords;
    Char *number = xmalloc((nChars+nWords)*sizeof(Char));
    struct wordent word[4];
    struct wordent base = { NULL, &word[0], &word[0] };
    word[0].word = number, word[0].next = &base, word[0].prev = &base;
    unsigned w = 0;			/* word number */
    /* Generate multiple words of length 2, 3, 5, then all the rest. */
    unsigned wBoundaries[4] = { 2-1, 2+3-1, 2+3+5-1, 0 };
    /* Ensure the last word has at least 4 Chars in it. */
    while (nWords >= 2 && nChars < (wBoundaries[nWords-2]+1) + 4)
	nWords--;
    wBoundaries[nWords-1] = 0xffffffff;	/* don't end word past this point */
    unsigned i;
    for (i = 0; i<nChars; i++) {
	/* In deference to the gawd awful add-mul hash, we won't use the worse
	 * case here (setting all Chars to 1), but assume mostly (or at least
	 * initially) ASCII data. */
	number[i+w] = '!';		/* 0x21 = 33 */

	if (i == wBoundaries[w]) {	/* end a word here and move to next */
	    w++;			/* next word */
	    number[i+w] = 0;		/* terminate */
	    word[w].word = &number[i+w+1];
	    word[w].next = &base, word[w].prev = &word[w-1];
	    word[w-1].next = &word[w], base.prev = &word[w];
	}
    }
    /* w is the index of the last word actually created. */
    number[nChars + w] = 0;		/* terminate last word */
    unsigned timeLimit = !samples;
    if (samples == 0)
	samples = 1000000000;
    doTiming(1);
    double sec;
    for (i = 0; i < samples; i++) {
	/* increment 4 digit base 255 number; last characters vary fastest */
	unsigned j = nChars-1 + w;
	while (1) {
	    if (++number[j] != 0)
		break;
	    /* else reset this digit and proceed to next one */
	    number[j] = 1;
	    if (&number[j] <= word[w].word)
		break;			/* stop at beginning of last word */
	    j--;
	}
	if (word[w].word[0] == '\n')
	    word[w].word[0]++;		/* suppress newline character */
	unsigned hash = hashhist(&base);
	hashes[hash2tableIndex(hash, length)]++;
	if (timeLimit && (i & 0x3ffff) == 0x3ffff) {
	    sec = doTiming(0);
	    if (sec > 10)
		break;
	}
    }
    if (i >= samples)
	sec = doTiming(0);
    else
	samples = i;			/* number we actually did */
    if (sec > 0.01) {
	xprintf("Hash %d (%d Char %u words) with %s: %d nsec/hash, %d mcps\n",
		samples, nChars, w+1, hashFcnName, (int)((sec/samples)*1e9),
		(int)((double)samples*nChars/sec/1e6));
    }
}
#endif /* DEBUG_HIST */

#ifdef DEBUG_HIST
static void
testHash(void)
{
    static const Char STRtestHashTimings[] =
	{ 't','e','s','t','H','a','s','h','T','i','m','i','n','g','s', 0 };
    struct varent *vp = adrof(STRtestHashTimings);
    if (vp && vp->vec) {
	unsigned hashes[4];		/* dummy place to put hashes */
	Char **vals = vp->vec;
	while (*vals) {
	    int length = getn(*vals);
	    unsigned words =
		(length < 5) ? 1 : (length < 25) ? 2 : (length < 75) ? 3 : 4;
	    if (length > 0)
		generateHashes(length, words, 0, hashes, 4);
	    vals++;
	}
    }
    unsigned length = 1024;
#ifdef PRIME_LENGTH			/* need good HTL */
    length = 1021;
#endif
    unsigned *hashes = xmalloc(length*sizeof(unsigned));
    memset(hashes, 0, length*sizeof(unsigned));
    /* Compute collision statistics for half full hashes modulo "length". */
    generateHashes(4, 1, length/2, hashes, length);
    /* Evaluate collisions by comparing occupancy rates (mean value 0.5).
     * One bin for each number of hits. */
    unsigned bins[155];
    memset(bins, 0, sizeof(bins));
    unsigned highest = 0;
    unsigned i;
    for (i = 0; i<length; i++) {
	unsigned hits = hashes[i];
	if (hits >= sizeof(bins)/sizeof(bins[0])) /* clip */
	    hits = highest = sizeof(bins)/sizeof(bins[0]) - 1;
	if (hits > highest)
	    highest = hits;
	bins[hits]++;
    }
    xprintf("Occupancy of %d buckets by %d hashes %d Chars %d word with %s\n",
	    length, length/2, 4, 1, hashFcnName);
    for (i = 0; i <= highest; i++) {
	xprintf(" %d buckets (%d%%) with %d hits\n",
		bins[i], bins[i]*100/length, i);
    }
    /* Count run lengths to evaluate linear rehashing effectiveness.  Estimate
     * a little corrupted by edge effects. */
    memset(bins, 0, sizeof(bins));
    highest = 0;
    for (i = 0; hashes[i] == 0; i++);	/* find first occupied bucket */
    unsigned run = 0;
    unsigned rehashed = 0;
    for (; i<length; i++) {
	unsigned hits = hashes[i];
	if (hits == 0 && rehashed > 0)
	    hits = 1 && rehashed--;
	else if (hits > 1)
	    rehashed += hits-1;
	if (hits)
	    run++;
	else {
	    /* a real free slot, count it */
	    if (run >= sizeof(bins)/sizeof(bins[0])) /* clip */
		run = highest = sizeof(bins)/sizeof(bins[0]) - 1;
	    if (run > highest)
		highest = run;
	    bins[run]++;
	    run = 0;
	}
    }
    /* Ignore the partial run at end as we ignored the beginning. */
    double merit = 0.0, entries = 0;
    for (i = 0; i <= highest; i++) {
	entries += bins[i]*i;		/* total hashed objects */
	merit += bins[i]*i*i;
    }
    xprintf("Rehash collision figure of merit %u (ideal=100), run lengths:\n",
	    (int)(100.0*merit/entries));
    for (i = 0; i <= highest; i++) {
	if (bins[i] != 0)
	    xprintf(" %d runs of length %d buckets\n", bins[i], i);
    }
    xfree(hashes);
}
#endif /* DEBUG_HIST */

/* Compares two word lists for equality. */
static int
heq(const struct wordent *a0, const struct wordent *b0)
{
    const struct wordent *a = a0->next, *b = b0->next;

    for (;;) {
	if (Strcmp(a->word, b->word) != 0)
	    return 0;
	a = a->next;
	b = b->next;
	if (a == a0)
	    return (b == b0) ? 1 : 0;
	if (b == b0)
	    return 0;
    }
}

/* Renumber entries following p, which we will be deleting. */
PG_STATIC void
renumberHist(struct Hist *p)
{
    int n = p->Href;
    while ((p = p->Hnext))
        p->Href = n--;
}

/* The hash table is implemented as an array of pointers to Hist entries.  Each
 * entry is located in the table using hash2tableIndex() and checking the
 * following entries in case of a collision (linear rehash).  Free entries in
 * the table are zero (0, NULL, emptyHTE).  Deleted entries that cannot yet be
 * freed are set to one (deletedHTE).  The Hist.Hhash member is non-zero iff
 * the entry is in the hash table.  When the hash table get too full, it is
 * reallocated to be approximately twice the history length (see
 * getHashTableSize). */
static struct Hist **histHashTable = NULL;
static unsigned histHashTableLength = 0; /* number of Hist pointers in table */

static struct Hist * const emptyHTE = NULL;
static struct Hist * const deletedHTE = (struct Hist *)1;

static struct {
    unsigned insertCount;
    unsigned removeCount;
    unsigned rehashes;
    int deleted;
} hashStats;

#ifdef DEBUG_HIST
void
checkHistHashTable(int print)
{
    unsigned occupied = 0;
    unsigned deleted = 0;
    unsigned i;
    for (i = 0; i<histHashTableLength; i++)
	if (histHashTable[i] == emptyHTE)
	    continue;
	else if (histHashTable[i] == deletedHTE)
	    deleted++;
	else
	    occupied++;
    if (print)
	xprintf("  found len %u occupied %u deleted %u\n",
		histHashTableLength, occupied, deleted);
    assert(deleted == hashStats.deleted);
}

static int doneTest = 0;

/* Main entry point for displaying history statistics and hash function
 * behavior. */
void
displayHistStats(const char *reason)
{
    /* Just hash statistics for now. */
    xprintf("%s history hash table len %u count %u (deleted %d)\n", reason,
	    histHashTableLength, histCount, hashStats.deleted);
    xprintf("  inserts %u rehashes %u%% each\n",
	    hashStats.insertCount,
	    (hashStats.insertCount
	     ? 100*hashStats.rehashes/hashStats.insertCount : 0));
    xprintf("  removes %u net %u\n",
	    hashStats.removeCount,
	    hashStats.insertCount - hashStats.removeCount);
    assert(hashStats.insertCount >= hashStats.removeCount);
    checkHistHashTable(1);
    memset(&hashStats, 0, sizeof(hashStats));
    if (!doneTest) {
	testHash();
	doneTest = 1;
    }
}
#else
void
displayHistStats(const char *reason)
{
    USE(reason);
}
#endif

static void
discardHistHashTable(void)
{
    if (histHashTable == NULL)
        return;
    displayHistStats("Discarding");
    xfree(histHashTable);
    histHashTable = NULL;
}

/* Computes a new hash table size, when the current one is too small. */
static unsigned
getHashTableSize(int hlen)
{
    unsigned target = hlen * 2;
    unsigned e = 5;
    unsigned size;
    while ((size = 1<<e) < target)
	e++;
#ifdef PRIME_LENGTH			/* need good HTL */
    /* Not all prime, but most are and none have factors smaller than 11. */
    return size+15;
#else
    assert((size & (size-1)) == 0);	/* must be a power of two */
    return size;
#endif
}

/* Create the hash table or resize, if necessary. */
static void
createHistHashTable(int hlen)
{
    if (hlen == 0) {
	discardHistHashTable();
        return;
    }
    if (hlen < 0) {
	if (histlen <= 0)
	    return;			/* no need for hash table */
	hlen = histlen;
    }
    if (histHashTable != NULL) {
	if (histCount < histHashTableLength * 3 / 4)
	    return;			/* good enough for now */
	discardHistHashTable();		/* too small */
    }
    histHashTableLength = getHashTableSize(
	hlen > (int)histCount ? hlen : (int)histCount);
    histHashTable = xmalloc(histHashTableLength * sizeof(struct Hist *));
    memset(histHashTable, 0, histHashTableLength * sizeof(struct Hist *));
    assert(histHashTable[0] == emptyHTE);

    /* Now insert all the entries on the history list into the hash table. */
    {
        struct Hist *hp;
        for (hp = &Histlist; (hp = hp->Hnext) != NULL;) {
            unsigned lpHash = hashhist(&hp->Hlex);
            assert(!hp->Hhash || hp->Hhash == lpHash);
            hp->Hhash = 0;              /* force insert to new hash table */
            insertHistHashTable(hp, lpHash);
        }
    }
}

/* Insert np into the hash table.  We assume that np is already on the
 * Histlist.  The specified hashval matches the new Hist entry but has not yet
 * been assigned to Hhash (or the element is already on the hash table). */
static void
insertHistHashTable(struct Hist *np, unsigned hashval)
{
    unsigned rehashes = 0;
    unsigned hi = 0;
    if (!histHashTable)
	return;
    if (np->Hhash != 0) {
        /* already in hash table */
        assert(hashval == np->Hhash);
        return;
    }
    assert(np != deletedHTE);
    /* Find a free (empty or deleted) slot, using linear rehash. */
    assert(histHashTable);
    for (rehashes = 0;
         ((hi = hash2tableIndex(hashval + rehashes, histHashTableLength)),
          histHashTable[hi] != emptyHTE && histHashTable[hi] != deletedHTE);
         rehashes++) {
        assert(np != histHashTable[hi]);
        if (rehashes >= histHashTableLength / 10) {
            /* Hash table is full, so grow it.  We assume the create function
             * will roughly double the size we give it.  Create initializes the
             * new table with everything on the Histlist, so we are done when
             * it returns.  */
#ifdef DEBUG_HIST
	    xprintf("Growing history hash table from %d ...",
		histHashTableLength);
	    flush();
#endif
            discardHistHashTable();
            createHistHashTable(histHashTableLength);
#ifdef DEBUG_HIST
	    xprintf("to %d.\n", histHashTableLength);
#endif
            return;
        }
    }
    /* Might be sensible to grow hash table if rehashes is "too big" here. */
    if (histHashTable[hi] == deletedHTE)
	hashStats.deleted--;
    histHashTable[hi] = np;
    np->Hhash = hashval;
    hashStats.insertCount++;
    hashStats.rehashes += rehashes;
}

/* Remove the 'np' entry from the hash table. */
static void
removeHistHashTable(struct Hist *np)
{
    unsigned hi = np->Hhash;
    if (!histHashTable || !hi)
        return;                         /* no hash table or not on it */
    /* find desired entry */
    while ((hi = hash2tableIndex(hi, histHashTableLength)),
           histHashTable[hi] != emptyHTE) {
        if (np == histHashTable[hi]) {
	    unsigned i;
	    unsigned deletes = 0;
	    histHashTable[hi] = deletedHTE; /* dummy, but non-zero entry */
	    /* now peek ahead to see if the dummies are really necessary. */
	    i = 1;
	    while (histHashTable[hash2tableIndex(hi+i, histHashTableLength)] ==
		   deletedHTE)
		i++;
	    if (histHashTable[hash2tableIndex(hi+i, histHashTableLength)] ==
		emptyHTE) {
		/* dummies are no longer necessary placeholders. */
		deletes = i;
		while (i-- > 0) {
		    histHashTable[hash2tableIndex(hi+i, histHashTableLength)] =
			emptyHTE;
		}
	    }
	    hashStats.deleted += 1 - deletes; /* delta deleted entries */
	    hashStats.removeCount++;
            return;
        }
        hi++;                           /* linear rehash */
    }
    assert(!"Hist entry not found in hash table");
}

/* Search the history hash table for a command matching lp, using hashval as
 * its hash value. */
static struct Hist *
findHistHashTable(struct wordent *lp, unsigned hashval)
{
    unsigned deleted = 0;		/* number of deleted entries skipped */
    unsigned hi = hashval;
    struct Hist *hp;
    if (!histHashTable)
	return NULL;
    while ((hi = hash2tableIndex(hi, histHashTableLength)),
           (hp = histHashTable[hi]) != emptyHTE) {
        if (hp == deletedHTE)
	    deleted++;
	else if (hp->Hhash == hashval && heq(lp, &(hp->Hlex)))
            return hp;
	if (deleted > (histHashTableLength>>4)) {
	    /* lots of deletes, so we need a sparser table. */
            discardHistHashTable();
            createHistHashTable(histHashTableLength);
	    return findHistHashTable(lp, hashval);
	}
        hi++;                           /* linear rehash */
    }
    return NULL;
}

/* When merge semantics are in use, find the approximate predecessor for the
 * new entry, so that the Htime entries are decreasing.  Return the entry just
 * before the first entry with equal times, so the caller can check for
 * duplicates.  When pTime is not NULL, use it as a starting point for search,
 * otherwise search from beginning (largest time value) of history list. */
PG_STATIC struct Hist *
mergeInsertionPoint(
    struct Hist *np,                      /* new entry to be inserted */
    struct Hist *pTime)                   /* hint about where to insert */
{
    struct Hist *pp, *p;
    if (histTail && histTail->Htime >= np->Htime)
	pTime = histTail;		/* new entry goes at the end */
    if (histMerg && histMerg != &Histlist && histMerg != Histlist.Hnext) {
	/* Check above and below previous insertion point, in case we're adding
	 * sequential times in the middle of the list (e.g. history -M). */
	if (histMerg->Htime >= np->Htime)
	    pTime = histMerg;
	else if (histMerg->Hprev->Htime >= np->Htime)
	    pTime = histMerg->Hprev;
    }
    if (pTime) {
        /* With hint, search up the list until Htime is greater.  We skip past
         * the equal ones, too, so our caller can elide duplicates. */
        pp = pTime;
        while (pp != &Histlist && pp->Htime <= np->Htime)
            pp = pp->Hprev;
    } else
        pp = &Histlist;
    /* Search down the list while current entry's time is too large. */
    while ((p = pp->Hnext) && (p->Htime > np->Htime))
            pp = p;                     /* advance insertion point */
    /* Remember recent position as hint for next time */
    histMerg = pp;
    return pp;
}

/* Bubble Hnum & Href in new entry down to pp through earlier part of list. */
PG_STATIC void bubbleHnumHrefDown(struct Hist *np, struct Hist *pp)
{
    struct Hist *p;
    for (p = Histlist.Hnext; p != pp->Hnext; p = p->Hnext) {
        /* swap Hnum & Href values of p and np. */
        int n = p->Hnum, r = p->Href;
        p->Hnum = np->Hnum; p->Href = np->Href;
        np->Hnum = n; np->Href = r;
    }
}

/* Enter new command into the history list according to current settings. */
struct Hist *
enthist(
  int event,				/* newly incremented global eventno */
  struct wordent *lp,
  int docopy,
  int mflg,				/* true if merge requested */
  int hlen)				/* -1 if unknown */
{
    struct Hist *p = NULL, *pp = &Histlist, *pTime = NULL;
    struct Hist *np;
    const Char *dp;
    unsigned lpHash = 0;                /* non-zero if hashing entries */

    if ((dp = varval(STRhistdup)) != STRNULL) {
	if (eq(dp, STRerase)) {
	    /* masaoki@akebono.tky.hp.com (Kobayashi Masaoki) */
            createHistHashTable(hlen);
            lpHash = hashhist(lp);
            assert(lpHash != 0);
            p = findHistHashTable(lp, lpHash);
            if (p) {
		if (Htime != 0 && p->Htime > Htime)
		    Htime = p->Htime;
                /* If we are merging, and the old entry is at the place we want
                 * to insert the new entry, then remember the place. */
                if (mflg && Htime != 0 && p->Hprev->Htime >= Htime)
                    pTime = p->Hprev;
		if (!fastMergeErase)
		    renumberHist(p);	/* Reset Href of subsequent entries */
                hremove(p);
		hfree(p);
                p = NULL;               /* so new entry is allocated below */
	    }
	}
	else if (eq(dp, STRall)) {
            createHistHashTable(hlen);
            lpHash = hashhist(lp);
            assert(lpHash != 0);
            p = findHistHashTable(lp, lpHash);
	    if (p)   /* p!=NULL, only update this entry's Htime below */
		eventno--;		/* not adding a new event */
	}
	else if (eq(dp, STRprev)) {
	    if (pp->Hnext && heq(lp, &(pp->Hnext->Hlex))) {
		p = pp->Hnext;
		eventno--;
	    }
	}
    }

    np = p ? p : xmalloc(sizeof(*np));

    /* Pick up timestamp set by lex() in Htime if reading saved history */
    if (Htime != 0) {
	np->Htime = Htime;
	Htime = 0;
    }
    else
        (void) time(&(np->Htime));

    if (p == np)
        return np;                      /* reused existing entry */

    /* Initialize the new entry. */
    np->Hnum = np->Href = event;
    if (docopy) {
        copylex(&np->Hlex, lp);
	if (histvalid)
	    np->histline = Strsave(histline.s);
	else
	    np->histline = NULL;
    }
    else {
	np->Hlex.next = lp->next;
	lp->next->prev = &np->Hlex;
	np->Hlex.prev = lp->prev;
        lp->prev->next = &np->Hlex;
        np->histline = NULL;
    }
    np->Hhash = 0;

    /* The head of history list is the default insertion point.
       If merging, advance insertion point, in pp, according to Htime. */
    /* XXX -- In histdup=all, Htime values can be non-monotonic. */
    if (mflg) {                         /* merge according to np->Htime */
        pp = mergeInsertionPoint(np, pTime);
        for (p = pp->Hnext; p && p->Htime == np->Htime; pp = p, p = p->Hnext) {
            if (heq(&p->Hlex, &np->Hlex)) {
                eventno--;              /* duplicate, so don't add new event */
                hfree(np);
                return (p);
              }
          }
        /* pp is now the last entry with time >= to np. */
	if (!fastMergeErase) {		/* renumber at end of loadhist */
	    /* Before inserting np after pp, bubble its Hnum & Href values down
	     * through the earlier part of list. */
	    bubbleHnumHrefDown(np, pp);
	}
    }
    else
        pp = &Histlist;                 /* insert at beginning of history */
    hinsert(np, pp);
    if (lpHash && hlen != 0)		/* erase & all modes use hash table */
        insertHistHashTable(np, lpHash);
    else
        discardHistHashTable();
    return (np);
}

static void
hfree(struct Hist *hp)
{
    assert(hp != histMerg);
    if (hp->Hhash)
        removeHistHashTable(hp);
    freelex(&hp->Hlex);
    if (hp->histline)
        xfree(hp->histline);
    xfree(hp);
}

PG_STATIC void
phist(struct Hist *hp, int hflg)
{
    if (hp->Href < 0)
	return;
    if (hflg & HIST_ONLY) {
	int old_output_raw;

       /*
        * Control characters have to be written as is (output_raw).
        * This way one can preserve special characters (like tab) in
        * the history file.
        * From: mveksler@vnet.ibm.com (Veksler Michael)
        */
	old_output_raw = output_raw;
        output_raw = 1;
	cleanup_push(&old_output_raw, output_raw_restore);
	if (hflg & HIST_TIME)
	    /* 
	     * Make file entry with history time in format:
	     * "+NNNNNNNNNN" (10 digits, left padded with ascii '0') 
	     */

	    xprintf("#+%010lu\n", (unsigned long)hp->Htime);

	if (HistLit && hp->histline)
	    xprintf("%S\n", hp->histline);
	else
	    prlex(&hp->Hlex);
        cleanup_until(&old_output_raw);
    }
    else {
	Char   *cp = str2short("%h\t%T\t%R\n");
	Char *p;
	struct varent *vp = adrof(STRhistory);

	if (vp && vp->vec != NULL && vp->vec[0] && vp->vec[1])
	    cp = vp->vec[1];

	p = tprintf(FMT_HISTORY, cp, NULL, hp->Htime, hp);
	cleanup_push(p, xfree);
	for (cp = p; *cp;)
	    xputwchar(*cp++);
	cleanup_until(p);
    }
}

PG_STATIC void
dophist(int n, int hflg)
{
    struct Hist *hp;
    if (setintr) {
	int old_pintr_disabled;

	pintr_push_enable(&old_pintr_disabled);
	cleanup_until(&old_pintr_disabled);
    }
    if ((hflg & HIST_REV) == 0) {
	/* Since the history list is stored most recent first, non-reversing
	 * print needs to print (backwards) up the list. */
	if ((unsigned)n >= histCount)
	    hp = histTail;
	else {
	    for (hp = Histlist.Hnext;
		 --n > 0 && hp->Hnext != NULL;
		 hp = hp->Hnext)
		;
	}
	if (hp == NULL)
	    return;			/* nothing to print */
	for (; hp != &Histlist; hp = hp->Hprev)
	    phist(hp, hflg);
    } else {
	for (hp = Histlist.Hnext; n-- > 0 && hp != NULL; hp = hp->Hnext)
	    phist(hp, hflg);
    }
}

/*ARGSUSED*/
void
dohist(Char **vp, struct command *c)
{
    int     n, hflg = 0;

    USE(c);
    if (getn(varval(STRhistory)) == 0)
	return;
    while (*++vp && **vp == '-') {
	Char   *vp2 = *vp;

	while (*++vp2)
	    switch (*vp2) {
	    case 'c':
		hflg |= HIST_CLEAR;
		break;
	    case 'h':
		hflg |= HIST_ONLY;
		break;
	    case 'r':
		hflg |= HIST_REV;
		break;
	    case 'S':
		hflg |= HIST_SAVE;
		break;
	    case 'L':
		hflg |= HIST_LOAD;
		break;
	    case 'M':
	    	hflg |= HIST_MERGE;
		break;
	    case 'T':
	    	hflg |= HIST_TIME;
		break;
	    default:
		stderror(ERR_HISTUS, "chrSLMT");
		break;
	    }
    }
    if (hflg & HIST_CLEAR) {
        struct Hist *np, *hp;
        for (hp = &Histlist; (np = hp->Hnext) != NULL;)
            hremove(np), hfree(np);
    }

    if (hflg & (HIST_LOAD | HIST_MERGE))
	loadhist(*vp, (hflg & HIST_MERGE) ? 1 : 0);
    else if (hflg & HIST_SAVE)
	rechist(*vp, 1);
    else {
	if (*vp)
	    n = getn(*vp);
	else {
	    n = getn(varval(STRhistory));
	}
	dophist(n, hflg);
    }
}


char *
fmthist(int fmt, ptr_t ptr)
{
    struct Hist *hp = ptr;
    char *buf;

    switch (fmt) {
    case 'h':
	return xasprintf("%6d", hp->Hnum);
    case 'R':
	if (HistLit && hp->histline)
	    return xasprintf("%S", hp->histline);
	else {
	    Char *istr, *ip;
	    char *p;

	    istr = sprlex(&hp->Hlex);
	    buf = xmalloc(Strlen(istr) * MB_LEN_MAX + 1);

	    for (p = buf, ip = istr; *ip != '\0'; ip++)
		p += one_wctomb(p, *ip);

	    *p = '\0';
	    xfree(istr);
	    return buf;
	}
    default:
	buf = xmalloc(1);
	buf[0] = '\0';
	return buf;
    }
}

static void
dotlock_cleanup(void* lockpath)
{
	dot_unlock((char*)lockpath);
}

/* Save history before exiting the shell. */
void
rechist(Char *fname, int ref)
{
    Char    *snum, *rs;
    int     fp, ftmp, oldidfds;
    struct varent *shist;
    char path[MAXPATHLEN];
    struct stat st;
    static Char   *dumphist[] = {STRhistory, STRmhT, 0, 0};

    if (fname == NULL && !ref) 
	return;
    /*
     * If $savehist is just set, we use the value of $history
     * else we use the value in $savehist
     */
    if (((snum = varval(STRsavehist)) == STRNULL) &&
	((snum = varval(STRhistory)) == STRNULL))
	snum = STRmaxint;


    if (fname == NULL) {
	if ((fname = varval(STRhistfile)) == STRNULL)
	    fname = Strspl(varval(STRhome), &STRtildothist[1]);
	else
	    fname = Strsave(fname);
    }
    else
	fname = globone(fname, G_ERROR);
    cleanup_push(fname, xfree);

    /*
     * The 'savehist merge' feature is intended for an environment
     * with numerous shells being in simultaneous use. Imagine
     * any kind of window system. All these shells 'share' the same 
     * ~/.history file for recording their command line history. 
     * We try to handle the case of multiple shells trying to merge
     * histories at the same time, by creating semi-unique filenames
     * and saving the history there first and then trying to rename
     * them in the proper history file.
     *
     * Users that like to nuke their environment require here an atomic
     * loadhist-creat-dohist(dumphist)-close sequence which is given
		 * by optional lock parameter to savehist.
     *
     * jw.
     */ 
    /*
     * We need the didfds stuff before loadhist otherwise
     * exec in a script will fail to print if merge is set.
     * From: mveksler@iil.intel.com (Veksler Michael)
     */
    oldidfds = didfds;
    didfds = 0;
    if ((shist = adrof(STRsavehist)) != NULL && shist->vec != NULL) {
	size_t i;
	int merge = 0, lock = 0;

	for (i = 1; shist->vec[i]; i++) {
	    if (eq(shist->vec[i], STRmerge))
		merge++;
	    if (eq(shist->vec[i], STRlock))
		lock++;
	}

	if (merge) {
	    if (lock) {
#ifndef WINNT_NATIVE
		char *lockpath = strsave(short2str(fname));
		cleanup_push(lockpath, xfree);
		/* Poll in 100 miliseconds interval to obtain the lock. */
		if ((dot_lock(lockpath, 100) == 0))
		    cleanup_push(lockpath, dotlock_cleanup);
#endif
	    }
	    loadhist(fname, 1);
	}
    }
    rs = randsuf();
    xsnprintf(path, sizeof(path), "%S.%S", fname, rs);
    xfree(rs);

    fp = xcreat(path, 0600);
    if (fp == -1) {
	didfds = oldidfds;
	cleanup_until(fname);
	return;
    }
    /* Try to preserve ownership and permissions of the original history file */
#ifndef WINNT_NATIVE
    if (stat(short2str(fname), &st) != -1) {
	TCSH_IGNORE(fchown(fp, st.st_uid, st.st_gid));
	TCSH_IGNORE(fchmod(fp, st.st_mode));
    }
#else
    UNREFERENCED_PARAMETER(st);
#endif
    ftmp = SHOUT;
    SHOUT = fp;
    dumphist[2] = snum;
    dohist(dumphist, NULL);
    xclose(fp);
    SHOUT = ftmp;
    didfds = oldidfds;
    (void)rename(path, short2str(fname));
    cleanup_until(fname);
}


/* This is the entry point for loading history data from a file. */
void
loadhist(Char *fname, int mflg)
{
    static Char   *loadhist_cmd[] = {STRsource, NULL, NULL, NULL};
    loadhist_cmd[1] = mflg ? STRmm : STRmh;

    if (fname != NULL)
	loadhist_cmd[2] = fname;
    else if ((fname = varval(STRhistfile)) != STRNULL)
	loadhist_cmd[2] = fname;
    else
	loadhist_cmd[2] = STRtildothist;

    dosource(loadhist_cmd, NULL);

    /* During history merging (enthist sees mflg set), we disable management of
     * Hnum and Href (because fastMergeErase is true).  So now reset all the
     * values based on the final ordering of the history list. */
    if (mflg) {
	int n = eventno;
        struct Hist *hp = &Histlist;
        while ((hp = hp->Hnext))
	    hp->Hnum = hp->Href = n--;
    }
}

void
sethistory(int n)
{
    histlen = n;
    discardExcess(histlen);
}

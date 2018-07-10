/*
 * bzip2 is written by Julian Seward <jseward@bzip.org>.
 * Adapted for busybox by Denys Vlasenko <vda.linux@googlemail.com>.
 * See README and LICENSE files in this directory for more information.
 */

/*-------------------------------------------------------------*/
/*--- Huffman coding low-level stuff                        ---*/
/*---                                             huffman.c ---*/
/*-------------------------------------------------------------*/

/* ------------------------------------------------------------------
This file is part of bzip2/libbzip2, a program and library for
lossless, block-sorting data compression.

bzip2/libbzip2 version 1.0.4 of 20 December 2006
Copyright (C) 1996-2006 Julian Seward <jseward@bzip.org>

Please read the WARNING, DISCLAIMER and PATENTS sections in the
README file.

This program is released under the terms of the license contained
in the file LICENSE.
------------------------------------------------------------------ */

/* #include "bzlib_private.h" */

/*---------------------------------------------------*/
#define WEIGHTOF(zz0)  ((zz0) & 0xffffff00)
#define DEPTHOF(zz1)   ((zz1) & 0x000000ff)
#define MYMAX(zz2,zz3) ((zz2) > (zz3) ? (zz2) : (zz3))

#define ADDWEIGHTS(zw1,zw2) \
	(WEIGHTOF(zw1)+WEIGHTOF(zw2)) | \
	(1 + MYMAX(DEPTHOF(zw1),DEPTHOF(zw2)))

#define UPHEAP(z) \
{ \
	int32_t zz, tmp; \
	zz = z; \
	tmp = heap[zz]; \
	while (weight[tmp] < weight[heap[zz >> 1]]) { \
		heap[zz] = heap[zz >> 1]; \
		zz >>= 1; \
	} \
	heap[zz] = tmp; \
}


/* 90 bytes, 0.3% of overall compress speed */
#if BZIP2_SPEED >= 1

/* macro works better than inline (gcc 4.2.1) */
#define DOWNHEAP1(heap, weight, Heap) \
{ \
	int32_t zz, yy, tmp; \
	zz = 1; \
	tmp = heap[zz]; \
	while (1) { \
		yy = zz << 1; \
		if (yy > nHeap) \
			break; \
		if (yy < nHeap \
		 && weight[heap[yy+1]] < weight[heap[yy]]) \
			yy++; \
		if (weight[tmp] < weight[heap[yy]]) \
			break; \
		heap[zz] = heap[yy]; \
		zz = yy; \
	} \
	heap[zz] = tmp; \
}

#else

static
void DOWNHEAP1(int32_t *heap, int32_t *weight, int32_t nHeap)
{
	int32_t zz, yy, tmp;
	zz = 1;
	tmp = heap[zz];
	while (1) {
		yy = zz << 1;
		if (yy > nHeap)
			break;
		if (yy < nHeap
		 && weight[heap[yy + 1]] < weight[heap[yy]])
			yy++;
		if (weight[tmp] < weight[heap[yy]])
			break;
		heap[zz] = heap[yy];
		zz = yy;
	}
	heap[zz] = tmp;
}

#endif

/*---------------------------------------------------*/
static
void BZ2_hbMakeCodeLengths(EState *s,
		uint8_t *len,
		int32_t *freq,
		int32_t alphaSize,
		int32_t maxLen)
{
	/*
	 * Nodes and heap entries run from 1.  Entry 0
	 * for both the heap and nodes is a sentinel.
	 */
	int32_t nNodes, nHeap, n1, n2, i, j, k;
	Bool  tooLong;

	/* bbox: moved to EState to save stack
	int32_t heap  [BZ_MAX_ALPHA_SIZE + 2];
	int32_t weight[BZ_MAX_ALPHA_SIZE * 2];
	int32_t parent[BZ_MAX_ALPHA_SIZE * 2];
	*/
#define heap   (s->BZ2_hbMakeCodeLengths__heap)
#define weight (s->BZ2_hbMakeCodeLengths__weight)
#define parent (s->BZ2_hbMakeCodeLengths__parent)

	for (i = 0; i < alphaSize; i++)
		weight[i+1] = (freq[i] == 0 ? 1 : freq[i]) << 8;

	while (1) {
		nNodes = alphaSize;
		nHeap = 0;

		heap[0] = 0;
		weight[0] = 0;
		parent[0] = -2;

		for (i = 1; i <= alphaSize; i++) {
			parent[i] = -1;
			nHeap++;
			heap[nHeap] = i;
			UPHEAP(nHeap);
		}

		AssertH(nHeap < (BZ_MAX_ALPHA_SIZE+2), 2001);

		while (nHeap > 1) {
			n1 = heap[1]; heap[1] = heap[nHeap]; nHeap--; DOWNHEAP1(heap, weight, nHeap);
			n2 = heap[1]; heap[1] = heap[nHeap]; nHeap--; DOWNHEAP1(heap, weight, nHeap);
			nNodes++;
			parent[n1] = parent[n2] = nNodes;
			weight[nNodes] = ADDWEIGHTS(weight[n1], weight[n2]);
			parent[nNodes] = -1;
			nHeap++;
			heap[nHeap] = nNodes;
			UPHEAP(nHeap);
		}

		AssertH(nNodes < (BZ_MAX_ALPHA_SIZE * 2), 2002);

		tooLong = False;
		for (i = 1; i <= alphaSize; i++) {
			j = 0;
			k = i;
			while (parent[k] >= 0) {
				k = parent[k];
				j++;
			}
			len[i-1] = j;
			if (j > maxLen)
				tooLong = True;
		}

		if (!tooLong)
			break;

		/* 17 Oct 04: keep-going condition for the following loop used
		to be 'i < alphaSize', which missed the last element,
		theoretically leading to the possibility of the compressor
		looping.  However, this count-scaling step is only needed if
		one of the generated Huffman code words is longer than
		maxLen, which up to and including version 1.0.2 was 20 bits,
		which is extremely unlikely.  In version 1.0.3 maxLen was
		changed to 17 bits, which has minimal effect on compression
		ratio, but does mean this scaling step is used from time to
		time, enough to verify that it works.

		This means that bzip2-1.0.3 and later will only produce
		Huffman codes with a maximum length of 17 bits.  However, in
		order to preserve backwards compatibility with bitstreams
		produced by versions pre-1.0.3, the decompressor must still
		handle lengths of up to 20. */

		for (i = 1; i <= alphaSize; i++) {
			j = weight[i] >> 8;
			/* bbox: yes, it is a signed division.
			 * don't replace with shift! */
			j = 1 + (j / 2);
			weight[i] = j << 8;
		}
	}
#undef heap
#undef weight
#undef parent
}


/*---------------------------------------------------*/
static
void BZ2_hbAssignCodes(int32_t *code,
		uint8_t *length,
		int32_t minLen,
		int32_t maxLen,
		int32_t alphaSize)
{
	int32_t n, vec, i;

	vec = 0;
	for (n = minLen; n <= maxLen; n++) {
		for (i = 0; i < alphaSize; i++) {
			if (length[i] == n) {
				code[i] = vec;
				vec++;
			}
		}
		vec <<= 1;
	}
}


/*-------------------------------------------------------------*/
/*--- end                                         huffman.c ---*/
/*-------------------------------------------------------------*/

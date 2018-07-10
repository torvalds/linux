/*
 * bzip2 is written by Julian Seward <jseward@bzip.org>.
 * Adapted for busybox by Denys Vlasenko <vda.linux@googlemail.com>.
 * See README and LICENSE files in this directory for more information.
 */

/*-------------------------------------------------------------*/
/*--- Private header file for the library.                  ---*/
/*---                                       bzlib_private.h ---*/
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

/* #include "bzlib.h" */

/*-- General stuff. --*/

typedef unsigned char Bool;

#define True  ((Bool)1)
#define False ((Bool)0)

#if BZ_LIGHT_DEBUG
static void bz_assert_fail(int errcode) NORETURN;
#define AssertH(cond, errcode) \
do { \
	if (!(cond)) \
		bz_assert_fail(errcode); \
} while (0)
#else
#define AssertH(cond, msg) do { } while (0)
#endif

#if BZ_DEBUG
#define AssertD(cond, msg) \
do { \
	if (!(cond)) \
		bb_error_msg_and_die("(debug build): internal error %s", msg); \
} while (0)
#else
#define AssertD(cond, msg) do { } while (0)
#endif


/*-- Header bytes. --*/

#define BZ_HDR_B 0x42   /* 'B' */
#define BZ_HDR_Z 0x5a   /* 'Z' */
#define BZ_HDR_h 0x68   /* 'h' */
#define BZ_HDR_0 0x30   /* '0' */

#define BZ_HDR_BZh0 0x425a6830

/*-- Constants for the back end. --*/

#define BZ_MAX_ALPHA_SIZE 258
#define BZ_MAX_CODE_LEN    23

#define BZ_RUNA 0
#define BZ_RUNB 1

#define BZ_N_GROUPS 6
#define BZ_G_SIZE   50
#define BZ_N_ITERS  4

#define BZ_MAX_SELECTORS (2 + (900000 / BZ_G_SIZE))


/*-- Stuff for doing CRCs. --*/

#define BZ_INITIALISE_CRC(crcVar) \
{ \
	crcVar = 0xffffffffL; \
}

#define BZ_FINALISE_CRC(crcVar) \
{ \
	crcVar = ~(crcVar); \
}

#define BZ_UPDATE_CRC(s, crcVar, cha) \
{ \
	crcVar = (crcVar << 8) ^ s->crc32table[(crcVar >> 24) ^ ((uint8_t)cha)]; \
}


/*-- States and modes for compression. --*/

#define BZ_M_IDLE      1
#define BZ_M_RUNNING   2
#define BZ_M_FLUSHING  3
#define BZ_M_FINISHING 4

#define BZ_S_OUTPUT    1
#define BZ_S_INPUT     2

#define BZ_N_RADIX 2
#define BZ_N_QSORT 12
#define BZ_N_SHELL 18
#define BZ_N_OVERSHOOT (BZ_N_RADIX + BZ_N_QSORT + BZ_N_SHELL + 2)


/*-- Structure holding all the compression-side stuff. --*/

typedef struct EState {
	/* pointer back to the struct bz_stream */
	bz_stream *strm;

	/* mode this stream is in, and whether inputting */
	/* or outputting data */
	uint8_t mode;
	uint8_t state;

	/* misc administratium */
	uint8_t blockSize100k;

	/* remembers avail_in when flush/finish requested */
/* bbox: not needed, strm->avail_in always has the same value */
/* commented out with '//#' throughout the code */
	/* uint32_t avail_in_expect; */

	/* for doing the block sorting */
	uint32_t *arr1;
	uint32_t *arr2;
	uint32_t *ftab;

	uint16_t *quadrant;
	int32_t  budget;

	/* aliases for arr1 and arr2 */
	uint32_t *ptr;
	uint8_t  *block;
	uint16_t *mtfv;
	uint8_t  *zbits;

	/* run-length-encoding of the input */
	uint32_t state_in_ch;
	int32_t  state_in_len;

	/* input and output limits and current posns */
	int32_t  nblock;
	int32_t  nblockMAX;
	//int32_t  numZ; // index into s->zbits[], replaced by pointer:
	uint8_t  *posZ;
	uint8_t  *state_out_pos;

	/* the buffer for bit stream creation */
	uint32_t bsBuff;
	int32_t  bsLive;

	/* guess what */
	uint32_t *crc32table;

	/* block and combined CRCs */
	uint32_t blockCRC;
	uint32_t combinedCRC;

	/* misc administratium */
	int32_t  blockNo;

	/* stuff for coding the MTF values */
	int32_t  nMTF;

	/* map of bytes used in block */
	int32_t  nInUse;
	Bool     inUse[256] ALIGNED(sizeof(long));
	uint8_t  unseqToSeq[256];

	/* stuff for coding the MTF values */
	int32_t  mtfFreq    [BZ_MAX_ALPHA_SIZE];
	uint8_t  selector   [BZ_MAX_SELECTORS];
	uint8_t  selectorMtf[BZ_MAX_SELECTORS];

	uint8_t  len[BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];

	/* stack-saving measures: these can be local, but they are too big */
	int32_t  sendMTFValues__code [BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
	int32_t  sendMTFValues__rfreq[BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
#if BZIP2_SPEED >= 5
	/* second dimension: only 3 needed; 4 makes index calculations faster */
	uint32_t sendMTFValues__len_pack[BZ_MAX_ALPHA_SIZE][4];
#endif
	int32_t  BZ2_hbMakeCodeLengths__heap  [BZ_MAX_ALPHA_SIZE + 2];
	int32_t  BZ2_hbMakeCodeLengths__weight[BZ_MAX_ALPHA_SIZE * 2];
	int32_t  BZ2_hbMakeCodeLengths__parent[BZ_MAX_ALPHA_SIZE * 2];

	int32_t  mainSort__copyStart[256];
	int32_t  mainSort__copyEnd[256];
} EState;


/*-- compression. --*/

static int32_t
BZ2_blockSort(EState*);

static void
BZ2_compressBlock(EState*, int);

static void
BZ2_bsInitWrite(EState*);

static void
BZ2_hbAssignCodes(int32_t*, uint8_t*, int32_t, int32_t, int32_t);

static void
BZ2_hbMakeCodeLengths(EState*, uint8_t*, int32_t*, int32_t, int32_t);

/*-------------------------------------------------------------*/
/*--- end                                   bzlib_private.h ---*/
/*-------------------------------------------------------------*/

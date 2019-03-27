/*-----------------------------------------------------------*/
/*--- Block recoverer program for bzip2                   ---*/
/*---                                      bzip2recover.c ---*/
/*-----------------------------------------------------------*/

/* ------------------------------------------------------------------
   This file is part of bzip2/libbzip2, a program and library for
   lossless, block-sorting data compression.

   bzip2/libbzip2 version 1.0.6 of 6 September 2010
   Copyright (C) 1996-2010 Julian Seward <jseward@bzip.org>

   Please read the WARNING, DISCLAIMER and PATENTS sections in the 
   README file.

   This program is released under the terms of the license contained
   in the file LICENSE.
   ------------------------------------------------------------------ */

/* This program is a complete hack and should be rewritten properly.
	 It isn't very complicated. */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>


/* This program records bit locations in the file to be recovered.
   That means that if 64-bit ints are not supported, we will not
   be able to recover .bz2 files over 512MB (2^32 bits) long.
   On GNU supported platforms, we take advantage of the 64-bit
   int support to circumvent this problem.  Ditto MSVC.

   This change occurred in version 1.0.2; all prior versions have
   the 512MB limitation.
*/
#ifdef __GNUC__
   typedef  unsigned long long int  MaybeUInt64;
#  define MaybeUInt64_FMT "%llu"
#else
#ifdef _MSC_VER
   typedef  unsigned __int64  MaybeUInt64;
#  define MaybeUInt64_FMT "%I64u"
#else
   typedef  unsigned int   MaybeUInt64;
#  define MaybeUInt64_FMT "%u"
#endif
#endif

typedef  unsigned int   UInt32;
typedef  int            Int32;
typedef  unsigned char  UChar;
typedef  char           Char;
typedef  unsigned char  Bool;
#define True    ((Bool)1)
#define False   ((Bool)0)


#define BZ_MAX_FILENAME 2000

Char inFileName[BZ_MAX_FILENAME];
Char outFileName[BZ_MAX_FILENAME];
Char progName[BZ_MAX_FILENAME];

MaybeUInt64 bytesOut = 0;
MaybeUInt64 bytesIn  = 0;


/*---------------------------------------------------*/
/*--- Header bytes                                ---*/
/*---------------------------------------------------*/

#define BZ_HDR_B 0x42                         /* 'B' */
#define BZ_HDR_Z 0x5a                         /* 'Z' */
#define BZ_HDR_h 0x68                         /* 'h' */
#define BZ_HDR_0 0x30                         /* '0' */
 

/*---------------------------------------------------*/
/*--- I/O errors                                  ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/
static void readError ( void )
{
   fprintf ( stderr,
             "%s: I/O error reading `%s', possible reason follows.\n",
            progName, inFileName );
   perror ( progName );
   fprintf ( stderr, "%s: warning: output file(s) may be incomplete.\n",
             progName );
   exit ( 1 );
}


/*---------------------------------------------*/
static void writeError ( void )
{
   fprintf ( stderr,
             "%s: I/O error reading `%s', possible reason follows.\n",
            progName, inFileName );
   perror ( progName );
   fprintf ( stderr, "%s: warning: output file(s) may be incomplete.\n",
             progName );
   exit ( 1 );
}


/*---------------------------------------------*/
static void mallocFail ( Int32 n )
{
   fprintf ( stderr,
             "%s: malloc failed on request for %d bytes.\n",
            progName, n );
   fprintf ( stderr, "%s: warning: output file(s) may be incomplete.\n",
             progName );
   exit ( 1 );
}


/*---------------------------------------------*/
static void tooManyBlocks ( Int32 max_handled_blocks )
{
   fprintf ( stderr,
             "%s: `%s' appears to contain more than %d blocks\n",
            progName, inFileName, max_handled_blocks );
   fprintf ( stderr,
             "%s: and cannot be handled.  To fix, increase\n",
             progName );
   fprintf ( stderr, 
             "%s: BZ_MAX_HANDLED_BLOCKS in bzip2recover.c, and recompile.\n",
             progName );
   exit ( 1 );
}



/*---------------------------------------------------*/
/*--- Bit stream I/O                              ---*/
/*---------------------------------------------------*/

typedef
   struct {
      FILE*  handle;
      Int32  buffer;
      Int32  buffLive;
      Char   mode;
   }
   BitStream;


/*---------------------------------------------*/
static BitStream* bsOpenReadStream ( FILE* stream )
{
   BitStream *bs = malloc ( sizeof(BitStream) );
   if (bs == NULL) mallocFail ( sizeof(BitStream) );
   bs->handle = stream;
   bs->buffer = 0;
   bs->buffLive = 0;
   bs->mode = 'r';
   return bs;
}


/*---------------------------------------------*/
static BitStream* bsOpenWriteStream ( FILE* stream )
{
   BitStream *bs = malloc ( sizeof(BitStream) );
   if (bs == NULL) mallocFail ( sizeof(BitStream) );
   bs->handle = stream;
   bs->buffer = 0;
   bs->buffLive = 0;
   bs->mode = 'w';
   return bs;
}


/*---------------------------------------------*/
static void bsPutBit ( BitStream* bs, Int32 bit )
{
   if (bs->buffLive == 8) {
      Int32 retVal = putc ( (UChar) bs->buffer, bs->handle );
      if (retVal == EOF) writeError();
      bytesOut++;
      bs->buffLive = 1;
      bs->buffer = bit & 0x1;
   } else {
      bs->buffer = ( (bs->buffer << 1) | (bit & 0x1) );
      bs->buffLive++;
   };
}


/*---------------------------------------------*/
/*--
   Returns 0 or 1, or 2 to indicate EOF.
--*/
static Int32 bsGetBit ( BitStream* bs )
{
   if (bs->buffLive > 0) {
      bs->buffLive --;
      return ( ((bs->buffer) >> (bs->buffLive)) & 0x1 );
   } else {
      Int32 retVal = getc ( bs->handle );
      if ( retVal == EOF ) {
         if (errno != 0) readError();
         return 2;
      }
      bs->buffLive = 7;
      bs->buffer = retVal;
      return ( ((bs->buffer) >> 7) & 0x1 );
   }
}


/*---------------------------------------------*/
static void bsClose ( BitStream* bs )
{
   Int32 retVal;

   if ( bs->mode == 'w' ) {
      while ( bs->buffLive < 8 ) {
         bs->buffLive++;
         bs->buffer <<= 1;
      };
      retVal = putc ( (UChar) (bs->buffer), bs->handle );
      if (retVal == EOF) writeError();
      bytesOut++;
      retVal = fflush ( bs->handle );
      if (retVal == EOF) writeError();
   }
   retVal = fclose ( bs->handle );
   if (retVal == EOF) {
      if (bs->mode == 'w') writeError(); else readError();
   }
   free ( bs );
}


/*---------------------------------------------*/
static void bsPutUChar ( BitStream* bs, UChar c )
{
   Int32 i;
   for (i = 7; i >= 0; i--)
      bsPutBit ( bs, (((UInt32) c) >> i) & 0x1 );
}


/*---------------------------------------------*/
static void bsPutUInt32 ( BitStream* bs, UInt32 c )
{
   Int32 i;

   for (i = 31; i >= 0; i--)
      bsPutBit ( bs, (c >> i) & 0x1 );
}


/*---------------------------------------------*/
static Bool endsInBz2 ( Char* name )
{
   Int32 n = strlen ( name );
   if (n <= 4) return False;
   return
      (name[n-4] == '.' &&
       name[n-3] == 'b' &&
       name[n-2] == 'z' &&
       name[n-1] == '2');
}


/*---------------------------------------------------*/
/*---                                             ---*/
/*---------------------------------------------------*/

/* This logic isn't really right when it comes to Cygwin. */
#ifdef _WIN32
#  define  BZ_SPLIT_SYM  '\\'  /* path splitter on Windows platform */
#else
#  define  BZ_SPLIT_SYM  '/'   /* path splitter on Unix platform */
#endif

#define BLOCK_HEADER_HI  0x00003141UL
#define BLOCK_HEADER_LO  0x59265359UL

#define BLOCK_ENDMARK_HI 0x00001772UL
#define BLOCK_ENDMARK_LO 0x45385090UL

/* Increase if necessary.  However, a .bz2 file with > 50000 blocks
   would have an uncompressed size of at least 40GB, so the chances
   are low you'll need to up this.
*/
#define BZ_MAX_HANDLED_BLOCKS 50000

MaybeUInt64 bStart [BZ_MAX_HANDLED_BLOCKS];
MaybeUInt64 bEnd   [BZ_MAX_HANDLED_BLOCKS];
MaybeUInt64 rbStart[BZ_MAX_HANDLED_BLOCKS];
MaybeUInt64 rbEnd  [BZ_MAX_HANDLED_BLOCKS];

Int32 main ( Int32 argc, Char** argv )
{
   FILE*       inFile;
   FILE*       outFile;
   BitStream*  bsIn, *bsWr;
   Int32       b, wrBlock, currBlock, rbCtr;
   MaybeUInt64 bitsRead;

   UInt32      buffHi, buffLo, blockCRC;
   Char*       p;

   strcpy ( progName, argv[0] );
   inFileName[0] = outFileName[0] = 0;

   fprintf ( stderr, 
             "bzip2recover 1.0.6: extracts blocks from damaged .bz2 files.\n" );

   if (argc != 2) {
      fprintf ( stderr, "%s: usage is `%s damaged_file_name'.\n",
                        progName, progName );
      switch (sizeof(MaybeUInt64)) {
         case 8:
            fprintf(stderr, 
                    "\trestrictions on size of recovered file: None\n");
            break;
         case 4:
            fprintf(stderr, 
                    "\trestrictions on size of recovered file: 512 MB\n");
            fprintf(stderr, 
                    "\tto circumvent, recompile with MaybeUInt64 as an\n"
                    "\tunsigned 64-bit int.\n");
            break;
         default:
            fprintf(stderr, 
                    "\tsizeof(MaybeUInt64) is not 4 or 8 -- "
                    "configuration error.\n");
            break;
      }
      exit(1);
   }

   if (strlen(argv[1]) >= BZ_MAX_FILENAME-20) {
      fprintf ( stderr, 
                "%s: supplied filename is suspiciously (>= %d chars) long.  Bye!\n",
                progName, (int)strlen(argv[1]) );
      exit(1);
   }

   strcpy ( inFileName, argv[1] );

   inFile = fopen ( inFileName, "rb" );
   if (inFile == NULL) {
      fprintf ( stderr, "%s: can't read `%s'\n", progName, inFileName );
      exit(1);
   }

   bsIn = bsOpenReadStream ( inFile );
   fprintf ( stderr, "%s: searching for block boundaries ...\n", progName );

   bitsRead = 0;
   buffHi = buffLo = 0;
   currBlock = 0;
   bStart[currBlock] = 0;

   rbCtr = 0;

   while (True) {
      b = bsGetBit ( bsIn );
      bitsRead++;
      if (b == 2) {
         if (bitsRead >= bStart[currBlock] &&
            (bitsRead - bStart[currBlock]) >= 40) {
            bEnd[currBlock] = bitsRead-1;
            if (currBlock > 0)
               fprintf ( stderr, "   block %d runs from " MaybeUInt64_FMT 
                                 " to " MaybeUInt64_FMT " (incomplete)\n",
                         currBlock,  bStart[currBlock], bEnd[currBlock] );
         } else
            currBlock--;
         break;
      }
      buffHi = (buffHi << 1) | (buffLo >> 31);
      buffLo = (buffLo << 1) | (b & 1);
      if ( ( (buffHi & 0x0000ffff) == BLOCK_HEADER_HI 
             && buffLo == BLOCK_HEADER_LO)
           || 
           ( (buffHi & 0x0000ffff) == BLOCK_ENDMARK_HI 
             && buffLo == BLOCK_ENDMARK_LO)
         ) {
         if (bitsRead > 49) {
            bEnd[currBlock] = bitsRead-49;
         } else {
            bEnd[currBlock] = 0;
         }
         if (currBlock > 0 &&
	     (bEnd[currBlock] - bStart[currBlock]) >= 130) {
            fprintf ( stderr, "   block %d runs from " MaybeUInt64_FMT 
                              " to " MaybeUInt64_FMT "\n",
                      rbCtr+1,  bStart[currBlock], bEnd[currBlock] );
            rbStart[rbCtr] = bStart[currBlock];
            rbEnd[rbCtr] = bEnd[currBlock];
            rbCtr++;
         }
         if (currBlock >= BZ_MAX_HANDLED_BLOCKS)
            tooManyBlocks(BZ_MAX_HANDLED_BLOCKS);
         currBlock++;

         bStart[currBlock] = bitsRead;
      }
   }

   bsClose ( bsIn );

   /*-- identified blocks run from 1 to rbCtr inclusive. --*/

   if (rbCtr < 1) {
      fprintf ( stderr,
                "%s: sorry, I couldn't find any block boundaries.\n",
                progName );
      exit(1);
   };

   fprintf ( stderr, "%s: splitting into blocks\n", progName );

   inFile = fopen ( inFileName, "rb" );
   if (inFile == NULL) {
      fprintf ( stderr, "%s: can't open `%s'\n", progName, inFileName );
      exit(1);
   }
   bsIn = bsOpenReadStream ( inFile );

   /*-- placate gcc's dataflow analyser --*/
   blockCRC = 0; bsWr = 0;

   bitsRead = 0;
   outFile = NULL;
   wrBlock = 0;
   while (True) {
      b = bsGetBit(bsIn);
      if (b == 2) break;
      buffHi = (buffHi << 1) | (buffLo >> 31);
      buffLo = (buffLo << 1) | (b & 1);
      if (bitsRead == 47+rbStart[wrBlock]) 
         blockCRC = (buffHi << 16) | (buffLo >> 16);

      if (outFile != NULL && bitsRead >= rbStart[wrBlock]
                          && bitsRead <= rbEnd[wrBlock]) {
         bsPutBit ( bsWr, b );
      }

      bitsRead++;

      if (bitsRead == rbEnd[wrBlock]+1) {
         if (outFile != NULL) {
            bsPutUChar ( bsWr, 0x17 ); bsPutUChar ( bsWr, 0x72 );
            bsPutUChar ( bsWr, 0x45 ); bsPutUChar ( bsWr, 0x38 );
            bsPutUChar ( bsWr, 0x50 ); bsPutUChar ( bsWr, 0x90 );
            bsPutUInt32 ( bsWr, blockCRC );
            bsClose ( bsWr );
         }
         if (wrBlock >= rbCtr) break;
         wrBlock++;
      } else
      if (bitsRead == rbStart[wrBlock]) {
         /* Create the output file name, correctly handling leading paths. 
            (31.10.2001 by Sergey E. Kusikov) */
         Char* split;
         Int32 ofs, k;
         for (k = 0; k < BZ_MAX_FILENAME; k++) 
            outFileName[k] = 0;
         strcpy (outFileName, inFileName);
         split = strrchr (outFileName, BZ_SPLIT_SYM);
         if (split == NULL) {
            split = outFileName;
         } else {
            ++split;
	 }
	 /* Now split points to the start of the basename. */
         ofs  = split - outFileName;
         sprintf (split, "rec%5d", wrBlock+1);
         for (p = split; *p != 0; p++) if (*p == ' ') *p = '0';
         strcat (outFileName, inFileName + ofs);

         if ( !endsInBz2(outFileName)) strcat ( outFileName, ".bz2" );

         fprintf ( stderr, "   writing block %d to `%s' ...\n",
                           wrBlock+1, outFileName );

         outFile = fopen ( outFileName, "wb" );
         if (outFile == NULL) {
            fprintf ( stderr, "%s: can't write `%s'\n",
                      progName, outFileName );
            exit(1);
         }
         bsWr = bsOpenWriteStream ( outFile );
         bsPutUChar ( bsWr, BZ_HDR_B );    
         bsPutUChar ( bsWr, BZ_HDR_Z );    
         bsPutUChar ( bsWr, BZ_HDR_h );    
         bsPutUChar ( bsWr, BZ_HDR_0 + 9 );
         bsPutUChar ( bsWr, 0x31 ); bsPutUChar ( bsWr, 0x41 );
         bsPutUChar ( bsWr, 0x59 ); bsPutUChar ( bsWr, 0x26 );
         bsPutUChar ( bsWr, 0x53 ); bsPutUChar ( bsWr, 0x59 );
      }
   }

   fprintf ( stderr, "%s: finished\n", progName );
   return 0;
}



/*-----------------------------------------------------------*/
/*--- end                                  bzip2recover.c ---*/
/*-----------------------------------------------------------*/

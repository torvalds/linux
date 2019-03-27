
/* A test program written to test robustness to decompression of
   corrupted data.  Usage is 
       unzcrash filename
   and the program will read the specified file, compress it (in memory),
   and then repeatedly decompress it, each time with a different bit of
   the compressed data inverted, so as to test all possible one-bit errors.
   This should not cause any invalid memory accesses.  If it does, 
   I want to know about it!

   PS.  As you can see from the above description, the process is
   incredibly slow.  A file of size eg 5KB will cause it to run for
   many hours.
*/

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


#include <stdio.h>
#include <assert.h>
#include "bzlib.h"

#define M_BLOCK 1000000

typedef unsigned char uchar;

#define M_BLOCK_OUT (M_BLOCK + 1000000)
uchar inbuf[M_BLOCK];
uchar outbuf[M_BLOCK_OUT];
uchar zbuf[M_BLOCK + 600 + (M_BLOCK / 100)];

int nIn, nOut, nZ;

static char *bzerrorstrings[] = {
       "OK"
      ,"SEQUENCE_ERROR"
      ,"PARAM_ERROR"
      ,"MEM_ERROR"
      ,"DATA_ERROR"
      ,"DATA_ERROR_MAGIC"
      ,"IO_ERROR"
      ,"UNEXPECTED_EOF"
      ,"OUTBUFF_FULL"
      ,"???"   /* for future */
      ,"???"   /* for future */
      ,"???"   /* for future */
      ,"???"   /* for future */
      ,"???"   /* for future */
      ,"???"   /* for future */
};

void flip_bit ( int bit )
{
   int byteno = bit / 8;
   int bitno  = bit % 8;
   uchar mask = 1 << bitno;
   //fprintf ( stderr, "(byte %d  bit %d  mask %d)",
   //          byteno, bitno, (int)mask );
   zbuf[byteno] ^= mask;
}

int main ( int argc, char** argv )
{
   FILE* f;
   int   r;
   int   bit;
   int   i;

   if (argc != 2) {
      fprintf ( stderr, "usage: unzcrash filename\n" );
      return 1;
   }

   f = fopen ( argv[1], "r" );
   if (!f) {
      fprintf ( stderr, "unzcrash: can't open %s\n", argv[1] );
      return 1;
   }

   nIn = fread ( inbuf, 1, M_BLOCK, f );
   fprintf ( stderr, "%d bytes read\n", nIn );

   nZ = M_BLOCK;
   r = BZ2_bzBuffToBuffCompress (
         zbuf, &nZ, inbuf, nIn, 9, 0, 30 );

   assert (r == BZ_OK);
   fprintf ( stderr, "%d after compression\n", nZ );

   for (bit = 0; bit < nZ*8; bit++) {
      fprintf ( stderr, "bit %d  ", bit );
      flip_bit ( bit );
      nOut = M_BLOCK_OUT;
      r = BZ2_bzBuffToBuffDecompress (
            outbuf, &nOut, zbuf, nZ, 0, 0 );
      fprintf ( stderr, " %d  %s ", r, bzerrorstrings[-r] );

      if (r != BZ_OK) {
         fprintf ( stderr, "\n" );
      } else {
         if (nOut != nIn) {
           fprintf(stderr, "nIn/nOut mismatch %d %d\n", nIn, nOut );
           return 1;
         } else {
           for (i = 0; i < nOut; i++)
             if (inbuf[i] != outbuf[i]) { 
                fprintf(stderr, "mismatch at %d\n", i ); 
                return 1; 
           }
           if (i == nOut) fprintf(stderr, "really ok!\n" );
         }
      }

      flip_bit ( bit );
   }

#if 0
   assert (nOut == nIn);
   for (i = 0; i < nOut; i++) {
     if (inbuf[i] != outbuf[i]) {
        fprintf ( stderr, "difference at %d !\n", i );
        return 1;
     }
   }
#endif

   fprintf ( stderr, "all ok\n" );
   return 0;
}

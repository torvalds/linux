/*
 * $Source: /homes/cvs/ftape-stacked/ftape/compressor/lzrw3.c,v $
 * $Revision: 1.1 $
 * $Date: 1997/10/05 19:12:29 $
 *
 * Implementation of Ross Williams lzrw3 algorithm. Adaption for zftape.
 *
 */

#include "../compressor/lzrw3.h"       /* Defines single exported function "compress".   */

/******************************************************************************/
/*                                                                            */
/*                                    LZRW3.C                                 */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/* Author  : Ross Williams.                                                   */
/* Date    : 30-Jun-1991.                                                     */
/* Release : 1.                                                               */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/* This file contains an implementation of the LZRW3 data compression         */
/* algorithm in C.                                                            */
/*                                                                            */
/* The algorithm is a general purpose compression algorithm that runs fast    */
/* and gives reasonable compression. The algorithm is a member of the Lempel  */
/* Ziv family of algorithms and bases its compression on the presence in the  */
/* data of repeated substrings.                                               */
/*                                                                            */
/* This algorithm is unpatented and the code is public domain. As the         */
/* algorithm is based on the LZ77 class of algorithms, it is unlikely to be   */
/* the subject of a patent challenge.                                         */
/*                                                                            */
/* Unlike the LZRW1 and LZRW1-A algorithms, the LZRW3 algorithm is            */
/* deterministic and is guaranteed to yield the same compressed               */
/* representation for a given file each time it is run.                       */
/*                                                                            */
/* The LZRW3 algorithm was originally designed and implemented                */
/* by Ross Williams on 31-Dec-1990.                                           */
/*                                                                            */
/* Here are the results of applying this code, compiled under THINK C 4.0     */
/* and running on a Mac-SE (8MHz 68000), to the standard calgary corpus.      */
/*                                                                            */
/*    +----------------------------------------------------------------+      */
/*    | DATA COMPRESSION TEST                                          |      */
/*    | =====================                                          |      */
/*    | Time of run     : Sun 30-Jun-1991 09:31PM                      |      */
/*    | Timing accuracy : One part in 100                              |      */
/*    | Context length  : 262144 bytes (= 256.0000K)                   |      */
/*    | Test suite      : Calgary Corpus Suite                         |      */
/*    | Files in suite  : 14                                           |      */
/*    | Algorithm       : LZRW3                                        |      */
/*    | Note: All averages are calculated from the un-rounded values.  |      */
/*    +----------------------------------------------------------------+      */
/*    | File Name   Length  CxB  ComLen  %Remn  Bits  Com K/s  Dec K/s |      */
/*    | ----------  ------  ---  ------  -----  ----  -------  ------- |      */
/*    | rpus:Bib.D  111261    1   55033   49.5  3.96    19.46    32.27 |      */
/*    | us:Book1.D  768771    3  467962   60.9  4.87    17.03    31.07 |      */
/*    | us:Book2.D  610856    3  317102   51.9  4.15    19.39    34.15 |      */
/*    | rpus:Geo.D  102400    1   82424   80.5  6.44    11.65    18.18 |      */
/*    | pus:News.D  377109    2  205670   54.5  4.36    17.14    27.47 |      */
/*    | pus:Obj1.D   21504    1   13027   60.6  4.85    13.40    18.95 |      */
/*    | pus:Obj2.D  246814    1  116286   47.1  3.77    19.31    30.10 |      */
/*    | s:Paper1.D   53161    1   27522   51.8  4.14    18.60    31.15 |      */
/*    | s:Paper2.D   82199    1   45160   54.9  4.40    18.45    32.84 |      */
/*    | rpus:Pic.D  513216    2  122388   23.8  1.91    35.29    51.05 |      */
/*    | us:Progc.D   39611    1   19669   49.7  3.97    18.87    30.64 |      */
/*    | us:Progl.D   71646    1   28247   39.4  3.15    24.34    40.66 |      */
/*    | us:Progp.D   49379    1   19377   39.2  3.14    23.91    39.23 |      */
/*    | us:Trans.D   93695    1   33481   35.7  2.86    25.48    40.37 |      */
/*    +----------------------------------------------------------------+      */
/*    | Average     224401    1  110953   50.0  4.00    20.17    32.72 |      */
/*    +----------------------------------------------------------------+      */
/*                                                                            */
/******************************************************************************/

/******************************************************************************/

/* The following structure is returned by the "compress" function below when  */
/* the user asks the function to return identifying information.              */
/* The most important field in the record is the working memory field which   */
/* tells the calling program how much working memory should be passed to      */
/* "compress" when it is called to perform a compression or decompression.    */
/* LZRW3 uses the same amount of memory during compression and decompression. */
/* For more information on this structure see "compress.h".                   */
  
#define U(X)            ((ULONG) X)
#define SIZE_P_BYTE     (U(sizeof(UBYTE *)))
#define SIZE_WORD       (U(sizeof(UWORD  )))
#define ALIGNMENT_FUDGE (U(16))
#define MEM_REQ ( U(4096)*(SIZE_P_BYTE) + ALIGNMENT_FUDGE )

static struct compress_identity identity =
{
 U(0x032DDEA8),                           /* Algorithm identification number. */
 MEM_REQ,                                 /* Working memory (bytes) required. */
 "LZRW3",                                 /* Name of algorithm.               */
 "1.0",                                   /* Version number of algorithm.     */
 "31-Dec-1990",                           /* Date of algorithm.               */
 "Public Domain",                         /* Copyright notice.                */
 "Ross N. Williams",                      /* Author of algorithm.             */
 "Renaissance Software",                  /* Affiliation of author.           */
 "Public Domain"                          /* Vendor of algorithm.             */
};
 
LOCAL void compress_compress  (UBYTE *,UBYTE *,ULONG,UBYTE *, LONG *);
LOCAL void compress_decompress(UBYTE *,UBYTE *,LONG, UBYTE *, ULONG *);

/******************************************************************************/

/* This function is the only function exported by this module.                */
/* Depending on its first parameter, the function can be requested to         */
/* compress a block of memory, decompress a block of memory, or to identify   */
/* itself. For more information, see the specification file "compress.h".     */

EXPORT void lzrw3_compress(
	UWORD     action,      /* Action to be performed.		*/
	UBYTE   *wrk_mem,	/* Address of working memory we can use.*/
	UBYTE   *src_adr,	/* Address of input data.		*/
	LONG     src_len,	/* Length  of input data.		*/
	UBYTE   *dst_adr,	/* Address to put output data.		*/
	void  *p_dst_len	/* Address of longword for length of output data.*/
)
{
 switch (action)
   {
    case COMPRESS_ACTION_IDENTITY:
       *((struct compress_identity **)p_dst_len)= &identity;
       break;
    case COMPRESS_ACTION_COMPRESS:
       compress_compress(wrk_mem,src_adr,src_len,dst_adr,(LONG *)p_dst_len);
       break;
    case COMPRESS_ACTION_DECOMPRESS:
       compress_decompress(wrk_mem,src_adr,src_len,dst_adr,(LONG *)p_dst_len);
       break;
   }
}

/******************************************************************************/
/*                                                                            */
/* BRIEF DESCRIPTION OF THE LZRW3 ALGORITHM                                   */
/* ========================================                                   */
/* The LZRW3 algorithm is identical to the LZRW1-A algorithm except that      */
/* instead of transmitting history offsets, it transmits hash table indexes.  */
/* In order to decode the indexes, the decompressor must maintain an          */
/* identical hash table. Copy items are straightforward:when the decompressor */
/* receives a copy item, it simply looks up the hash table to translate the   */
/* index into a pointer into the data already decompressed. To update the     */
/* hash table, it replaces the same table entry with a pointer to the start   */
/* of the newly decoded phrase. The tricky part is with literal items, for at */
/* the time that the decompressor receives a literal item the decompressor    */
/* does not have the three bytes in the Ziv (that the compressor has) to      */
/* perform the three-byte hash. To solve this problem, in LZRW3, both the     */
/* compressor and decompressor are wired up so that they "buffer" these       */
/* literals and update their hash tables only when three bytes are available. */
/* This makes the maximum buffering 2 bytes.                                  */
/*                                                                            */
/* Replacement of offsets by hash table indexes yields a few percent extra    */
/* compression at the cost of some speed. LZRW3 is slower than LZRW1, LZRW1-A */
/* and LZRW2, but yields better compression.                                  */
/*                                                                            */
/* Extra compression could be obtained by using a hash table of depth two.    */
/* However, increasing the depth above one incurs a significant decrease in   */
/* compression speed which was not considered worthwhile. Another reason for  */
/* keeping the depth down to one was to allow easy comparison with the        */
/* LZRW1-A and LZRW2 algorithms so as to demonstrate the exact effect of the  */
/* use of direct hash indexes.                                                */
/*                                                                            */
/*                                  +---+                                     */
/*                                  |___|4095                                 */
/*                                  |___|                                     */
/*              +---------------------*_|<---+   /----+---\                   */
/*              |                   |___|    +---|Hash    |                   */
/*              |                   |___|        |Function|                   */
/*              |                   |___|        \--------/                   */
/*              |                   |___|0            ^                       */
/*              |                   +---+             |                       */
/*              |                   Hash        +-----+                       */
/*              |                   Table       |                             */
/*              |                              ---                            */
/*              v                              ^^^                            */
/*      +-------------------------------------|----------------+              */
/*      ||||||||||||||||||||||||||||||||||||||||||||||||||||||||              */
/*      +-------------------------------------|----------------+              */
/*      |                                     |1......18|      |              */
/*      |<------- Lempel=History ------------>|<--Ziv-->|      |              */
/*      |     (=bytes already processed)      |<-Still to go-->|              */
/*      |<-------------------- INPUT BLOCK ------------------->|              */
/*                                                                            */
/* The diagram above for LZRW3 looks almost identical to the diagram for      */
/* LZRW1. The difference is that in LZRW3, the compressor transmits hash      */
/* table indices instead of Lempel offsets. For this to work, the             */
/* decompressor must maintain a hash table as well as the compressor and both */
/* compressor and decompressor must "buffer" literals, as the decompressor    */
/* cannot hash phrases commencing with a literal until another two bytes have */
/* arrived.                                                                   */
/*                                                                            */
/*  LZRW3 Algorithm Execution Summary                                         */
/*  ---------------------------------                                         */
/*  1. Hash the first three bytes of the Ziv to yield a hash table index h.   */
/*  2. Look up the hash table yielding history pointer p.                     */
/*  3. Match where p points with the Ziv. If there is a match of three or     */
/*     more bytes, code those bytes (in the Ziv) as a copy item, otherwise    */
/*     code the next byte in the Ziv as a literal item.                       */
/*  4. Update the hash table as possible subject to the constraint that only  */
/*     phrases commencing three bytes back from the Ziv can be hashed and     */
/*     entered into the hash table. (This enables the decompressor to keep    */
/*     pace). See the description and code for more details.                  */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*                     DEFINITION OF COMPRESSED FILE FORMAT                   */
/*                     ====================================                   */
/*  * A compressed file consists of a COPY FLAG followed by a REMAINDER.      */
/*  * The copy flag CF uses up four bytes with the first byte being the       */
/*    least significant.                                                      */
/*  * If CF=1, then the compressed file represents the remainder of the file  */
/*    exactly. Otherwise CF=0 and the remainder of the file consists of zero  */
/*    or more GROUPS, each of which represents one or more bytes.             */
/*  * Each group consists of two bytes of CONTROL information followed by     */
/*    sixteen ITEMs except for the last group which can contain from one      */
/*    to sixteen items.                                                       */
/*  * An item can be either a LITERAL item or a COPY item.                    */
/*  * Each item corresponds to a bit in the control bytes.                    */
/*  * The first control byte corresponds to the first 8 items in the group    */
/*    with bit 0 corresponding to the first item in the group and bit 7 to    */
/*    the eighth item in the group.                                           */
/*  * The second control byte corresponds to the second 8 items in the group  */
/*    with bit 0 corresponding to the ninth item in the group and bit 7 to    */
/*    the sixteenth item in the group.                                        */
/*  * A zero bit in a control word means that the corresponding item is a     */
/*    literal item. A one bit corresponds to a copy item.                     */
/*  * A literal item consists of a single byte which represents itself.       */
/*  * A copy item consists of two bytes that represent from 3 to 18 bytes.    */
/*  * The first  byte in a copy item will be denoted C1.                      */
/*  * The second byte in a copy item will be denoted C2.                      */
/*  * Bits will be selected using square brackets.                            */
/*    For example: C1[0..3] is the low nibble of the first control byte.      */
/*    of copy item C1.                                                        */
/*  * The LENGTH of a copy item is defined to be C1[0..3]+3 which is a number */
/*    in the range [3,18].                                                    */
/*  * The INDEX of a copy item is defined to be C1[4..7]*256+C2[0..8] which   */
/*    is a number in the range [0,4095].                                      */
/*  * A copy item represents the sequence of bytes                            */
/*       text[POS-OFFSET..POS-OFFSET+LENGTH-1] where                          */
/*          text   is the entire text of the uncompressed string.             */
/*          POS    is the index in the text of the character following the    */
/*                   string represented by all the items preceeding the item  */
/*                   being defined.                                           */
/*          OFFSET is obtained from INDEX by looking up the hash table.       */
/*                                                                            */
/******************************************************************************/

/* The following #define defines the length of the copy flag that appears at  */
/* the start of the compressed file. The value of four bytes was chosen       */
/* because the fast_copy routine on my Macintosh runs faster if the source    */
/* and destination blocks are relatively longword aligned.                    */
/* The actual flag data appears in the first byte. The rest are zeroed so as  */
/* to normalize the compressed representation (i.e. not non-deterministic).   */
#define FLAG_BYTES 4

/* The following #defines define the meaning of the values of the copy        */
/* flag at the start of the compressed file.                                  */
#define FLAG_COMPRESS 0     /* Signals that output was result of compression. */
#define FLAG_COPY     1     /* Signals that output was simply copied over.    */

/* The 68000 microprocessor (on which this algorithm was originally developed */
/* is fussy about non-aligned arrays of words. To avoid these problems the    */
/* following macro can be used to "waste" from 0 to 3 bytes so as to align    */
/* the argument pointer.                                                      */
#define ULONG_ALIGN_UP(X) ((((ULONG)X)+sizeof(ULONG)-1)&~(sizeof(ULONG)-1))


/* The following constant defines the maximum length of an uncompressed item. */
/* This definition must not be changed; its value is hardwired into the code. */
/* The longest number of bytes that can be spanned by a single item is 18     */
/* for the longest copy item.                                                 */
#define MAX_RAW_ITEM (18)

/* The following constant defines the maximum length of an uncompressed group.*/
/* This definition must not be changed; its value is hardwired into the code. */
/* A group contains at most 16 items which explains this definition.          */
#define MAX_RAW_GROUP (16*MAX_RAW_ITEM)

/* The following constant defines the maximum length of a compressed group.   */
/* This definition must not be changed; its value is hardwired into the code. */
/* A compressed group consists of two control bytes followed by up to 16      */
/* compressed items each of which can have a maximum length of two bytes.     */
#define MAX_CMP_GROUP (2+16*2)

/* The following constant defines the number of entries in the hash table.    */
/* This definition must not be changed; its value is hardwired into the code. */
#define HASH_TABLE_LENGTH (4096)

/* LZRW3, unlike LZRW1(-A), must initialize its hash table so as to enable    */
/* the compressor and decompressor to stay in step maintaining identical hash */
/* tables. In an early version of the algorithm, the tables were simply       */
/* initialized to zero and a check for zero was included just before the      */
/* matching code. However, this test costs time. A better solution is to      */
/* initialize all the entries in the hash table to point to a constant        */
/* string. The decompressor does the same. This solution requires no extra    */
/* test. The contents of the string do not matter so long as the string is    */
/* the same for the compressor and decompressor and contains at least         */
/* MAX_RAW_ITEM bytes. I chose consecutive decimal digits because they do not */
/* have white space problems (e.g. there is no chance that the compiler will  */
/* replace more than one space by a TAB) and because they make the length of  */
/* the string obvious by inspection.                                          */
#define START_STRING_18 ((UBYTE *) "123456789012345678")

/* In this algorithm, hash values have to be calculated at more than one      */
/* point. The following macro neatens the code up for this.                   */
#define HASH(PTR) \
   (((40543*(((*(PTR))<<8)^((*((PTR)+1))<<4)^(*((PTR)+2))))>>4) & 0xFFF)

/******************************************************************************/

/* Input  : Hand over the required amount of working memory in p_wrk_mem.     */
/* Input  : Specify input block using p_src_first and src_len.                */
/* Input  : Point p_dst_first to the start of the output zone (OZ).           */
/* Input  : Point p_dst_len to a ULONG to receive the output length.          */
/* Input  : Input block and output zone must not overlap.                     */
/* Output : Length of output block written to *p_dst_len.                     */
/* Output : Output block in Mem[p_dst_first..p_dst_first+*p_dst_len-1]. May   */
/* Output : write in OZ=Mem[p_dst_first..p_dst_first+src_len+MAX_CMP_GROUP-1].*/
/* Output : Upon completion guaranteed *p_dst_len<=src_len+FLAG_BYTES.        */
LOCAL void compress_compress(UBYTE *p_wrk_mem,
			     UBYTE *p_src_first, ULONG  src_len,
			     UBYTE *p_dst_first, LONG  *p_dst_len)
{
 /* p_src and p_dst step through the source and destination blocks.           */
 register UBYTE *p_src = p_src_first;
 register UBYTE *p_dst = p_dst_first;
 
 /* The following variables are never modified and are used in the            */
 /* calculations that determine when the main loop terminates.                */
 UBYTE *p_src_post  = p_src_first+src_len;
 UBYTE *p_dst_post  = p_dst_first+src_len;
 UBYTE *p_src_max1  = p_src_first+src_len-MAX_RAW_ITEM;
 UBYTE *p_src_max16 = p_src_first+src_len-MAX_RAW_ITEM*16;
 
 /* The variables 'p_control' and 'control' are used to buffer control bits.  */
 /* Before each group is processed, the next two bytes of the output block    */
 /* are set aside for the control word for the group about to be processed.   */
 /* 'p_control' is set to point to the first byte of that word. Meanwhile,    */
 /* 'control' buffers the control bits being generated during the processing  */
 /* of the group. Instead of having a counter to keep track of how many items */
 /* have been processed (=the number of bits in the control word), at the     */
 /* start of each group, the top word of 'control' is filled with 1 bits.     */
 /* As 'control' is shifted for each item, the 1 bits in the top word are     */
 /* absorbed or destroyed. When they all run out (i.e. when the top word is   */
 /* all zero bits, we know that we are at the end of a group.                 */
# define TOPWORD 0xFFFF0000
 UBYTE *p_control;
 register ULONG control=TOPWORD;
 
 /* THe variable 'hash' always points to the first element of the hash table. */
 UBYTE **hash= (UBYTE **)  ULONG_ALIGN_UP(p_wrk_mem);
 
 /* The following two variables represent the literal buffer. p_h1 points to  */
 /* the hash table entry corresponding to the youngest literal. p_h2 points   */
 /* to the hash table entry corresponding to the second youngest literal.     */
 /* Note: p_h1=0=>p_h2=0 because zero values denote absence of a pending      */
 /* literal. The variables are initialized to zero meaning an empty "buffer". */
 UBYTE **p_h1=NULL;
 UBYTE **p_h2=NULL;
  
 /* To start, we write the flag bytes. Being optimistic, we set the flag to   */
 /* FLAG_COMPRESS. The remaining flag bytes are zeroed so as to keep the      */
 /* algorithm deterministic.                                                  */
 *p_dst++=FLAG_COMPRESS;
 {UWORD i; for (i=2;i<=FLAG_BYTES;i++) *p_dst++=0;}

 /* Reserve the first word of output as the control word for the first group. */
 /* Note: This is undone at the end if the input block is empty.              */
 p_control=p_dst; p_dst+=2;
 
 /* Initialize all elements of the hash table to point to a constant string.  */
 /* Use of an unrolled loop speeds this up considerably.                      */
 {UWORD i; UBYTE **p_h=hash;
#  define ZH *p_h++=START_STRING_18
  for (i=0;i<256;i++)     /* 256=HASH_TABLE_LENGTH/16. */
    {ZH;ZH;ZH;ZH;
     ZH;ZH;ZH;ZH;
     ZH;ZH;ZH;ZH;
     ZH;ZH;ZH;ZH;}
 }

 /* The main loop processes either 1 or 16 items per iteration. As its        */
 /* termination logic is complicated, I have opted for an infinite loop       */
 /* structure containing 'break' and 'goto' statements.                       */
 while (TRUE)
   {/* Begin main processing loop. */
   
    /* Note: All the variables here except unroll should be defined within    */
    /*       the inner loop. Unfortunately the loop hasn't got a block.       */
     register UBYTE *p;         /* Scans through targ phrase during matching. */
     register UBYTE *p_ziv= NULL ;     /* Points to first byte of current Ziv.       */
     register UWORD unroll;     /* Loop counter for unrolled inner loop.      */
     register UWORD index;      /* Index of current hash table entry.         */
     register UBYTE **p_h0 = NULL ;     /* Pointer to current hash table entry.       */
     
    /* Test for overrun and jump to overrun code if necessary.                */
    if (p_dst>p_dst_post)
       goto overrun;
       
    /* The following cascade of if statements efficiently catches and deals   */
    /* with varying degrees of closeness to the end of the input block.       */
    /* When we get very close to the end, we stop updating the table and      */
    /* code the remaining bytes as literals. This makes the code simpler.     */
    unroll=16;
    if (p_src>p_src_max16)
      {
       unroll=1;
       if (p_src>p_src_max1)
         {
          if (p_src==p_src_post)
             break;
          else
             goto literal;
         }
      }
         
    /* This inner unrolled loop processes 'unroll' (whose value is either 1   */
    /* or 16) items. I have chosen to implement this loop with labels and     */
    /* gotos to heighten the ease with which the loop may be implemented with */
    /* a single decrement and branch instruction in assembly language and     */
    /* also because the labels act as highly readable place markers.          */
    /* (Also because we jump into the loop for endgame literals (see above)). */
    
    begin_unrolled_loop:
    
       /* To process the next phrase, we hash the next three bytes and use    */
       /* the resultant hash table index to look up the hash table. A pointer */
       /* to the entry is stored in p_h0 so as to avoid an array lookup. The  */
       /* hash table entry *p_h0 is looked up yielding a pointer p to a       */
       /* potential match of the Ziv in the history.                          */
       index=HASH(p_src);
       p_h0=&hash[index];
       p=*p_h0;
       
       /* Having looked up the candidate position, we are in a position to    */
       /* attempt a match. The match loop has been unrolled using the PS      */
       /* macro so that failure within the first three bytes automatically    */
       /* results in the literal branch being taken. The coding is simple.    */
       /* p_ziv saves p_src so we can let p_src wander.                       */
#       define PS *p++!=*p_src++
       p_ziv=p_src;
       if (PS || PS || PS)
         {
          /* Literal. */
          
          /* Code the literal byte as itself and a zero control bit.          */
          p_src=p_ziv; literal: *p_dst++=*p_src++; control&=0xFFFEFFFF;
          
          /* We have just coded a literal. If we had two pending ones, that   */
          /* makes three and we can update the hash table.                    */
          if (p_h2!=0)
             {*p_h2=p_ziv-2;}
             
          /* In any case, rotate the hash table pointers for next time. */
          p_h2=p_h1; p_h1=p_h0;
          
         }
       else
         {
          /* Copy */
          
          /* Match up to 15 remaining bytes using an unrolled loop and code. */
#if 0
          PS || PS || PS || PS || PS || PS || PS || PS ||
          PS || PS || PS || PS || PS || PS || PS || p_src++;
#else     
          if (
               !( PS || PS || PS || PS || PS || PS || PS || PS ||
                  PS || PS || PS || PS || PS || PS || PS ) 
             ) p_src++;
#endif
          *p_dst++=((index&0xF00)>>4)|(--p_src-p_ziv-3);
          *p_dst++=index&0xFF;
          
          /* As we have just coded three bytes, we are now in a position to   */
          /* update the hash table with the literal bytes that were pending   */
          /* upon the arrival of extra context bytes.                         */
          if (p_h1!=0)
            {
             if (p_h2)
               {*p_h2=p_ziv-2; p_h2=NULL;}
             *p_h1=p_ziv-1; p_h1=NULL;
            }
            
          /* In any case, we can update the hash table based on the current   */
          /* position as we just coded at least three bytes in a copy items.  */
          *p_h0=p_ziv;
          
         }
       control>>=1;
                
       /* This loop is all set up for a decrement and jump instruction! */
#ifndef linux
`    end_unrolled_loop: if (--unroll) goto begin_unrolled_loop;
#else
    /* end_unrolled_loop: */ if (--unroll) goto begin_unrolled_loop;
#endif

    /* At this point it will nearly always be the end of a group in which     */
    /* case, we have to do some control-word processing. However, near the    */
    /* end of the input block, the inner unrolled loop is only executed once. */
    /* This necessitates the 'if' test.                                       */
    if ((control&TOPWORD)==0)
      {
       /* Write the control word to the place we saved for it in the output. */
       *p_control++=  control     &0xFF;
       *p_control  = (control>>8) &0xFF;

       /* Reserve the next word in the output block for the control word */
       /* for the group about to be processed.                           */
       p_control=p_dst; p_dst+=2;
       
       /* Reset the control bits buffer. */
       control=TOPWORD;
      }
          
   } /* End main processing loop. */
   
 /* After the main processing loop has executed, all the input bytes have     */
 /* been processed. However, the control word has still to be written to the  */
 /* word reserved for it in the output at the start of the most recent group. */
 /* Before writing, the control word has to be shifted so that all the bits   */
 /* are in the right place. The "empty" bit positions are filled with 1s      */
 /* which partially fill the top word.                                        */
 while(control&TOPWORD) control>>=1;
 *p_control++= control     &0xFF;
 *p_control++=(control>>8) &0xFF;
 
 /* If the last group contained no items, delete the control word too.        */
 if (p_control==p_dst) p_dst-=2;
 
 /* Write the length of the output block to the dst_len parameter and return. */
 *p_dst_len=p_dst-p_dst_first;                           
 return;
 
 /* Jump here as soon as an overrun is detected. An overrun is defined to     */
 /* have occurred if p_dst>p_dst_first+src_len. That is, the moment the       */
 /* length of the output written so far exceeds the length of the input block.*/
 /* The algorithm checks for overruns at least at the end of each group       */
 /* which means that the maximum overrun is MAX_CMP_GROUP bytes.              */
 /* Once an overrun occurs, the only thing to do is to set the copy flag and  */
 /* copy the input over.                                                      */
 overrun:
#if 0
 *p_dst_first=FLAG_COPY;
 fast_copy(p_src_first,p_dst_first+FLAG_BYTES,src_len);
 *p_dst_len=src_len+FLAG_BYTES;
#else
 fast_copy(p_src_first,p_dst_first,src_len);
 *p_dst_len= -src_len; /* return a negative number to indicate uncompressed data */
#endif
}

/******************************************************************************/

/* Input  : Hand over the required amount of working memory in p_wrk_mem.     */
/* Input  : Specify input block using p_src_first and src_len.                */
/* Input  : Point p_dst_first to the start of the output zone.                */
/* Input  : Point p_dst_len to a ULONG to receive the output length.          */
/* Input  : Input block and output zone must not overlap. User knows          */
/* Input  : upperbound on output block length from earlier compression.       */
/* Input  : In any case, maximum expansion possible is nine times.            */
/* Output : Length of output block written to *p_dst_len.                     */
/* Output : Output block in Mem[p_dst_first..p_dst_first+*p_dst_len-1].       */
/* Output : Writes only  in Mem[p_dst_first..p_dst_first+*p_dst_len-1].       */
LOCAL void compress_decompress( UBYTE *p_wrk_mem,
				UBYTE *p_src_first, LONG   src_len,
				UBYTE *p_dst_first, ULONG *p_dst_len)
{
 /* Byte pointers p_src and p_dst scan through the input and output blocks.   */
 register UBYTE *p_src = p_src_first+FLAG_BYTES;
 register UBYTE *p_dst = p_dst_first;
 /* we need to avoid a SEGV when trying to uncompress corrupt data */
 register UBYTE *p_dst_post = p_dst_first + *p_dst_len;

 /* The following two variables are never modified and are used to control    */
 /* the main loop.                                                            */
 UBYTE *p_src_post  = p_src_first+src_len;
 UBYTE *p_src_max16 = p_src_first+src_len-(MAX_CMP_GROUP-2);
 
 /* The hash table is the only resident of the working memory. The hash table */
 /* contains HASH_TABLE_LENGTH=4096 pointers to positions in the history. To  */
 /* keep Macintoshes happy, it is longword aligned.                           */
 UBYTE **hash = (UBYTE **) ULONG_ALIGN_UP(p_wrk_mem);

 /* The variable 'control' is used to buffer the control bits which appear in */
 /* groups of 16 bits (control words) at the start of each compressed group.  */
 /* When each group is read, bit 16 of the register is set to one. Whenever   */
 /* a new bit is needed, the register is shifted right. When the value of the */
 /* register becomes 1, we know that we have reached the end of a group.      */
 /* Initializing the register to 1 thus instructs the code to follow that it  */
 /* should read a new control word immediately.                               */
 register ULONG control=1;
 
 /* The value of 'literals' is always in the range 0..3. It is the number of  */
 /* consecutive literal items just seen. We have to record this number so as  */
 /* to know when to update the hash table. When literals gets to 3, there     */
 /* have been three consecutive literals and we can update at the position of */
 /* the oldest of the three.                                                  */
 register UWORD literals=0;
 
 /* Check the leading copy flag to see if the compressor chose to use a copy  */
 /* operation instead of a compression operation. If a copy operation was     */
 /* used, then all we need to do is copy the data over, set the output length */
 /* and return.                                                               */
#if 0
 if (*p_src_first==FLAG_COPY)
   {
    fast_copy(p_src_first+FLAG_BYTES,p_dst_first,src_len-FLAG_BYTES);
    *p_dst_len=src_len-FLAG_BYTES;
    return;
   }
#else
  if ( src_len < 0 )
  {                                            
   fast_copy(p_src_first,p_dst_first,-src_len );
   *p_dst_len = (ULONG)-src_len;
   return;
  }
#endif
   
 /* Initialize all elements of the hash table to point to a constant string.  */
 /* Use of an unrolled loop speeds this up considerably.                      */
 {UWORD i; UBYTE **p_h=hash;
#  define ZJ *p_h++=START_STRING_18
  for (i=0;i<256;i++)     /* 256=HASH_TABLE_LENGTH/16. */
    {ZJ;ZJ;ZJ;ZJ;
     ZJ;ZJ;ZJ;ZJ;
     ZJ;ZJ;ZJ;ZJ;
     ZJ;ZJ;ZJ;ZJ;}
 }

 /* The outer loop processes either 1 or 16 items per iteration depending on  */
 /* how close p_src is to the end of the input block.                         */
 while (p_src!=p_src_post)
   {/* Start of outer loop */
   
    register UWORD unroll;   /* Counts unrolled loop executions.              */
    
    /* When 'control' has the value 1, it means that the 16 buffered control  */
    /* bits that were read in at the start of the current group have all been */
    /* shifted out and that all that is left is the 1 bit that was injected   */
    /* into bit 16 at the start of the current group. When we reach the end   */
    /* of a group, we have to load a new control word and inject a new 1 bit. */
    if (control==1)
      {
       control=0x10000|*p_src++;
       control|=(*p_src++)<<8;
      }

    /* If it is possible that we are within 16 groups from the end of the     */
    /* input, execute the unrolled loop only once, else process a whole group */
    /* of 16 items by looping 16 times.                                       */
    unroll= p_src<=p_src_max16 ? 16 : 1;

    /* This inner loop processes one phrase (item) per iteration. */
    while (unroll--)
      { /* Begin unrolled inner loop. */
      
       /* Process a literal or copy item depending on the next control bit. */
       if (control&1)
         {
          /* Copy item. */
          
          register UBYTE *p;           /* Points to place from which to copy. */
          register UWORD lenmt;        /* Length of copy item minus three.    */
          register UBYTE **p_hte;      /* Pointer to current hash table entry.*/
          register UBYTE *p_ziv=p_dst; /* Pointer to start of current Ziv.    */
          
          /* Read and dismantle the copy word. Work out from where to copy.   */
          lenmt=*p_src++;
          p_hte=&hash[((lenmt&0xF0)<<4)|*p_src++];
          p=*p_hte;
          lenmt&=0xF;
          
          /* Now perform the copy using a half unrolled loop. */
          *p_dst++=*p++;
          *p_dst++=*p++;
          *p_dst++=*p++;
          while (lenmt--)
             *p_dst++=*p++;
                 
          /* Because we have just received 3 or more bytes in a copy item     */
          /* (whose bytes we have just installed in the output), we are now   */
          /* in a position to flush all the pending literal hashings that had */
          /* been postponed for lack of bytes.                                */
          if (literals>0)
            {
             register UBYTE *r=p_ziv-literals;
             hash[HASH(r)]=r;
             if (literals==2)
                {r++; hash[HASH(r)]=r;}
             literals=0;
            }
            
          /* In any case, we can immediately update the hash table with the   */
          /* current position. We don't need to do a HASH(...) to work out    */
          /* where to put the pointer, as the compressor just told us!!!      */
          *p_hte=p_ziv;
          
         }
       else
         {
          /* Literal item. */
          
          /* Copy over the literal byte. */
          *p_dst++=*p_src++;
          
          /* If we now have three literals waiting to be hashed into the hash */
          /* table, we can do one of them now (because there are three).      */
          if (++literals == 3)
             {register UBYTE *p=p_dst-3; hash[HASH(p)]=p; literals=2;}
         }
          
       /* Shift the control buffer so the next control bit is in bit 0. */
       control>>=1;
#if 1
       if (p_dst > p_dst_post) 
       {
	       /* Shit: we tried to decompress corrupt data */
	       *p_dst_len = 0;
	       return;
       }
#endif
      } /* End unrolled inner loop. */
               
   } /* End of outer loop */
   
 /* Write the length of the decompressed data before returning. */
  *p_dst_len=p_dst-p_dst_first;
}

/******************************************************************************/
/*                               End of LZRW3.C                               */
/******************************************************************************/

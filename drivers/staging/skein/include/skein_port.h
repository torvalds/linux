#ifndef _SKEIN_PORT_H_
#define _SKEIN_PORT_H_
/*******************************************************************
**
** Platform-specific definitions for Skein hash function.
**
** Source code author: Doug Whiting, 2008.
**
** This algorithm and source code is released to the public domain.
**
** Many thanks to Brian Gladman for his portable header files.
**
** To port Skein to an "unsupported" platform, change the definitions
** in this file appropriately.
** 
********************************************************************/

typedef unsigned int    uint_t;             /* native unsigned integer */
typedef uint8_t         u08b_t;             /*  8-bit unsigned integer */
typedef uint64_t        u64b_t;             /* 64-bit unsigned integer */

#ifndef RotL_64
#define RotL_64(x,N)    (((x) << (N)) | ((x) >> (64-(N))))
#endif

/*
 * Skein is "natively" little-endian (unlike SHA-xxx), for optimal
 * performance on x86 CPUs.  The Skein code requires the following
 * definitions for dealing with endianness:
 *
 *    SKEIN_NEED_SWAP:  0 for little-endian, 1 for big-endian
 *    Skein_Put64_LSB_First
 *    Skein_Get64_LSB_First
 *    Skein_Swap64
 *
 * If SKEIN_NEED_SWAP is defined at compile time, it is used here
 * along with the portable versions of Put64/Get64/Swap64, which 
 * are slow in general.
 *
 * Otherwise, an "auto-detect" of endianness is attempted below.
 * If the default handling doesn't work well, the user may insert
 * platform-specific code instead (e.g., for big-endian CPUs).
 *
 */
#define SKEIN_NEED_SWAP   (0)
/* below two prototype assume we are handed aligned data */
#define Skein_Put64_LSB_First(dst08,src64,bCnt) memcpy(dst08,src64,bCnt)
#define Skein_Get64_LSB_First(dst64,src08,wCnt) memcpy(dst64,src08,8*(wCnt))

/*
 ******************************************************************
 *      Provide any definitions still needed.
 ******************************************************************
 */
#ifndef Skein_Swap64  /* swap for big-endian, nop for little-endian */
#if     SKEIN_NEED_SWAP
#define Skein_Swap64(w64)                       \
  ( (( ((u64b_t)(w64))       & 0xFF) << 56) |   \
    (((((u64b_t)(w64)) >> 8) & 0xFF) << 48) |   \
    (((((u64b_t)(w64)) >>16) & 0xFF) << 40) |   \
    (((((u64b_t)(w64)) >>24) & 0xFF) << 32) |   \
    (((((u64b_t)(w64)) >>32) & 0xFF) << 24) |   \
    (((((u64b_t)(w64)) >>40) & 0xFF) << 16) |   \
    (((((u64b_t)(w64)) >>48) & 0xFF) <<  8) |   \
    (((((u64b_t)(w64)) >>56) & 0xFF)      ) )
#else
#define Skein_Swap64(w64)  (w64)
#endif
#endif  /* ifndef Skein_Swap64 */


#ifndef Skein_Put64_LSB_First
void    Skein_Put64_LSB_First(u08b_t *dst,const u64b_t *src,size_t bCnt)
#ifdef  SKEIN_PORT_CODE /* instantiate the function code here? */
    { /* this version is fully portable (big-endian or little-endian), but slow */
    size_t n;

    for (n=0;n<bCnt;n++)
        dst[n] = (u08b_t) (src[n>>3] >> (8*(n&7)));
    }
#else
    ;    /* output only the function prototype */
#endif
#endif   /* ifndef Skein_Put64_LSB_First */


#ifndef Skein_Get64_LSB_First
void    Skein_Get64_LSB_First(u64b_t *dst,const u08b_t *src,size_t wCnt)
#ifdef  SKEIN_PORT_CODE /* instantiate the function code here? */
    { /* this version is fully portable (big-endian or little-endian), but slow */
    size_t n;

    for (n=0;n<8*wCnt;n+=8)
        dst[n/8] = (((u64b_t) src[n  ])      ) +
                   (((u64b_t) src[n+1]) <<  8) +
                   (((u64b_t) src[n+2]) << 16) +
                   (((u64b_t) src[n+3]) << 24) +
                   (((u64b_t) src[n+4]) << 32) +
                   (((u64b_t) src[n+5]) << 40) +
                   (((u64b_t) src[n+6]) << 48) +
                   (((u64b_t) src[n+7]) << 56) ;
    }
#else
    ;    /* output only the function prototype */
#endif
#endif   /* ifndef Skein_Get64_LSB_First */

#endif   /* ifndef _SKEIN_PORT_H_ */

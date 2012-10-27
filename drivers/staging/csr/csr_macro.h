#ifndef CSR_MACRO_H__
#define CSR_MACRO_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/types.h>

#define FALSE	(0)
#define TRUE	(1)

/*------------------------------------------------------------------*/
/* Endian conversion */
/*------------------------------------------------------------------*/
#define CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr)        (((u16) ((u8 *) (ptr))[0]) | ((u16) ((u8 *) (ptr))[1]) << 8)
#define CSR_GET_UINT32_FROM_LITTLE_ENDIAN(ptr)        (((u32) ((u8 *) (ptr))[0]) | ((u32) ((u8 *) (ptr))[1]) << 8 | \
                                                       ((u32) ((u8 *) (ptr))[2]) << 16 | ((u32) ((u8 *) (ptr))[3]) << 24)
#define CSR_COPY_UINT16_TO_LITTLE_ENDIAN(uint, ptr)    ((u8 *) (ptr))[0] = ((u8) ((uint) & 0x00FF)); \
    ((u8 *) (ptr))[1] = ((u8) ((uint) >> 8))
#define CSR_COPY_UINT32_TO_LITTLE_ENDIAN(uint, ptr)    ((u8 *) (ptr))[0] = ((u8) ((uint) & 0x000000FF)); \
    ((u8 *) (ptr))[1] = ((u8) (((uint) >> 8) & 0x000000FF)); \
    ((u8 *) (ptr))[2] = ((u8) (((uint) >> 16) & 0x000000FF)); \
    ((u8 *) (ptr))[3] = ((u8) (((uint) >> 24) & 0x000000FF))

/*------------------------------------------------------------------*/
/* Misc */
/*------------------------------------------------------------------*/
/* Use this macro on unused local variables that cannot be removed (such as
   unused function parameters). This will quell warnings from certain compilers
   and static code analysis tools like Lint and Valgrind. */
#define CSR_UNUSED(x) ((void) (x))

#endif

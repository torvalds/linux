/**
* \file $Id: bsp_host.h,v 1.3 2009/07/07 14:20:30 justin Exp $
*
* \brief Host and OS dependent type definitions, macro's and functions
*
*/

/*
* $(c) 2004-2005,2007-2009 Trident Microsystems, Inc. - All rights reserved.
*
* This software and related documentation (the 'Software') are intellectual
* property owned by Trident and are copyright of Trident, unless specifically
* noted otherwise.
*
* Any use of the Software is permitted only pursuant to the terms of the
* license agreement, if any, which accompanies, is included with or applicable
* to the Software ('License Agreement') or upon express written consent of
* Trident. Any copying, reproduction or redistribution of the Software in
* whole or in part by any means not in accordance with the License Agreement
* or as agreed in writing by Trident is expressly prohibited.
*
* THE SOFTWARE IS WARRANTED, IF AT ALL, ONLY ACCORDING TO THE TERMS OF THE
* LICENSE AGREEMENT. EXCEPT AS WARRANTED IN THE LICENSE AGREEMENT THE SOFTWARE
* IS DELIVERED 'AS IS' AND TRIDENT HEREBY DISCLAIMS ALL WARRANTIES AND
* CONDITIONS WITH REGARD TO THE SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
* AND CONDITIONS OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, QUIT
* ENJOYMENT, TITLE AND NON-INFRINGEMENT OF ANY THIRD PARTY INTELLECTUAL
* PROPERTY OR OTHER RIGHTS WHICH MAY RESULT FROM THE USE OR THE INABILITY
* TO USE THE SOFTWARE.
*
* IN NO EVENT SHALL TRIDENT BE LIABLE FOR INDIRECT, INCIDENTAL, CONSEQUENTIAL,
* PUNITIVE, SPECIAL OR OTHER DAMAGES WHATSOEVER INCLUDING WITHOUT LIMITATION,
* DAMAGES FOR LOSS OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS
* INFORMATION, AND THE LIKE, ARISING OUT OF OR RELATING TO THE USE OF OR THE
* INABILITY TO USE THE SOFTWARE, EVEN IF TRIDENT HAS BEEN ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGES, EXCEPT PERSONAL INJURY OR DEATH RESULTING FROM
* TRIDENT'S NEGLIGENCE.                                                        $
*
*/
#ifndef __DRXBSP_HOST_H__
#define __DRXBSP_HOST_H__
/*-------------------------------------------------------------------------
INCLUDES
-------------------------------------------------------------------------*/
#include "bsp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*-------------------------------------------------------------------------
TYPEDEFS
-------------------------------------------------------------------------*/


/*-------------------------------------------------------------------------
DEFINES
-------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------
Exported FUNCTIONS
-------------------------------------------------------------------------*/
DRXStatus_t DRXBSP_HST_Init( void );

DRXStatus_t DRXBSP_HST_Term( void );

void* DRXBSP_HST_Memcpy( void *to, void *from, u32_t n);

int DRXBSP_HST_Memcmp( void *s1, void *s2, u32_t n);

u32_t DRXBSP_HST_Clock( void );

DRXStatus_t DRXBSP_HST_Sleep( u32_t n );

/*-------------------------------------------------------------------------
THE END
-------------------------------------------------------------------------*/
#ifdef __cplusplus
}
#endif

#endif /* __DRXBSP_HOST_H__ */

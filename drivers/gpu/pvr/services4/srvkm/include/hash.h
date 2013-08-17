/*************************************************************************/ /*!
@Title          Self scaling hash tables
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements simple self scaling hash tables.
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef _HASH_H_
#define _HASH_H_

#include "img_types.h"
#include "osfunc.h"

#if defined (__cplusplus)
extern "C" {
#endif

/*
 * Keys passed to the comparsion function are only guaranteed to
 * be aligned on an IMG_UINTPTR_T boundary. 
 */
typedef IMG_UINT32 HASH_FUNC(IMG_SIZE_T uKeySize, IMG_VOID *pKey, IMG_UINT32 uHashTabLen);
typedef IMG_BOOL HASH_KEY_COMP(IMG_SIZE_T uKeySize, IMG_VOID *pKey1, IMG_VOID *pKey2);

typedef struct _HASH_TABLE_ HASH_TABLE;

typedef PVRSRV_ERROR (*HASH_pfnCallback) (
	IMG_UINTPTR_T k,
	IMG_UINTPTR_T v
);

/*!
******************************************************************************
    @Function       HASH_Func_Default

    @Description    Hash function intended for hashing keys composed of
                    IMG_UINTPTR_T arrays.
	                	
    @Input          uKeySize - the size of the hash key, in bytes.
    @Input          pKey - a pointer to the key to hash.
    @Input          uHashTabLen - the length of the hash table. 

    @Return         The hash value.
******************************************************************************/
IMG_UINT32 HASH_Func_Default (IMG_SIZE_T uKeySize, IMG_VOID *pKey, IMG_UINT32 uHashTabLen);

/*!
******************************************************************************
    @Function       HASH_Key_Comp_Default

    @Description    Compares keys composed of IMG_UINTPTR_T arrays.
	                	
    @Input          uKeySize - the size of the hash key, in bytes.
    @Input          pKey1 - pointer to first hash key to compare.
    @Input          pKey2 - pointer to second hash key to compare.

    @Return         IMG_TRUE  - the keys match.
                    IMG_FALSE - the keys don't match.
******************************************************************************/
IMG_BOOL HASH_Key_Comp_Default (IMG_SIZE_T uKeySize, IMG_VOID *pKey1, IMG_VOID *pKey2);

/*!
******************************************************************************
    @Function       HASH_Create_Extended

    @Description    Create a self scaling hash table, using the supplied
                    key size, and the supllied hash and key comparsion
                    functions.
	                	
    @Input          uInitialLen - initial and minimum length of the
                        hash table, where the length refers to the number
                        of entries in the hash table, not its size in
                        bytes.
    @Input          uKeySize - the size of the key, in bytes.
    @Input          pfnHashFunc - pointer to hash function.
    @Input          pfnKeyComp - pointer to key comparsion function.

    @Return         IMG_NULL or hash table handle.
******************************************************************************/
HASH_TABLE * HASH_Create_Extended (IMG_UINT32 uInitialLen, IMG_SIZE_T uKeySize, HASH_FUNC *pfnHashFunc, HASH_KEY_COMP *pfnKeyComp);

/*!
******************************************************************************
    @Function       HASH_Create

    @Description    Create a self scaling hash table with a key
                    consisting of a single IMG_UINTPTR_T, and using
                    the default hash and key comparison functions.
	                	
    @Input          uInitialLen - initial and minimum length of the
                        hash table, where the length refers to the
                        number of entries in the hash table, not its size
                        in bytes.

    @Return         IMG_NULL or hash table handle.
******************************************************************************/
HASH_TABLE * HASH_Create (IMG_UINT32 uInitialLen);

/*!
******************************************************************************
    @Function       HASH_Delete

    @Description    Delete a hash table created by HASH_Create_Extended or
                    HASH_Create.  All entries in the table must have been
                    removed before calling this function.
	                	
    @Input          pHash - hash table

    @Return         None
******************************************************************************/
IMG_VOID HASH_Delete (HASH_TABLE *pHash);

/*!
******************************************************************************
    @Function       HASH_Insert_Extended

    @Description    Insert a key value pair into a hash table created
                    with HASH_Create_Extended.
	                	
    @Input          pHash - the hash table.
    @Input          pKey - pointer to the key.
    @Input          v - the value associated with the key.

    @Return         IMG_TRUE - success.
                    IMG_FALSE - failure.
******************************************************************************/
IMG_BOOL HASH_Insert_Extended (HASH_TABLE *pHash, IMG_VOID *pKey, IMG_UINTPTR_T v);

/*!
******************************************************************************
    @Function       HASH_Insert

    @Description    Insert a key value pair into a hash table created with
                    HASH_Create.
	                	
    @Input          pHash - the hash table.
    @Input          k - the key value.
    @Input          v - the value associated with the key.

    @Return         IMG_TRUE - success.
                    IMG_FALSE - failure.
******************************************************************************/
IMG_BOOL HASH_Insert (HASH_TABLE *pHash, IMG_UINTPTR_T k, IMG_UINTPTR_T v);

/*!
******************************************************************************
    @Function       HASH_Remove_Extended

    @Description    Remove a key from a hash table created with
                    HASH_Create_Extended.
	                	
    @Input          pHash - the hash table.
    @Input          pKey - pointer to key.

    @Return         0 if the key is missing, or the value associated
                    with the key.
******************************************************************************/
IMG_UINTPTR_T HASH_Remove_Extended(HASH_TABLE *pHash, IMG_VOID *pKey);

/*!
******************************************************************************
    @Function       HASH_Remove

    @Description    Remove a key value pair from a hash table created
                    with HASH_Create.
	                	
    @Input          pHash - the hash table
    @Input          k - the key

    @Return         0 if the key is missing, or the value associated
                    with the key.
******************************************************************************/
IMG_UINTPTR_T HASH_Remove (HASH_TABLE *pHash, IMG_UINTPTR_T k);

/*!
******************************************************************************
    @Function       HASH_Retrieve_Extended

    @Description    Retrieve a value from a hash table created with
                    HASH_Create_Extended.
	                	
    @Input          pHash - the hash table.
    @Input          pKey - pointer to the key.

    @Return         0 if the key is missing, or the value associated with
                    the key.
******************************************************************************/
IMG_UINTPTR_T HASH_Retrieve_Extended (HASH_TABLE *pHash, IMG_VOID *pKey);

/*!
******************************************************************************
    @Function       HASH_Retrieve

    @Description    Retrieve a value from a hash table created with
                    HASH_Create.
	                	
    @Input          pHash - the hash table
    @Input          k - the key

    @Return         0 if the key is missing, or the value associated with
                    the key.
******************************************************************************/
IMG_UINTPTR_T HASH_Retrieve (HASH_TABLE *pHash, IMG_UINTPTR_T k);

/*!
******************************************************************************
    @Function       HASH_Interate

    @Description    Iterate over every entry in the hash table

    @Input          pHash - the old hash table
    @Input          HASH_pfnCallback - the size of the old hash table

    @Return         Callback error if any, otherwise PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR HASH_Iterate(HASH_TABLE *pHash, HASH_pfnCallback pfnCallback);

#ifdef HASH_TRACE
/*!
******************************************************************************
    @Function       HASH_Dump

    @Description    Dump out some information about a hash table.
	                	
    @Input          pHash - the hash table

    @Return         None
******************************************************************************/
IMG_VOID HASH_Dump (HASH_TABLE *pHash);
#endif

#if defined (__cplusplus)
}
#endif

#endif /* _HASH_H_ */

/******************************************************************************
 End of file (hash.h)
******************************************************************************/



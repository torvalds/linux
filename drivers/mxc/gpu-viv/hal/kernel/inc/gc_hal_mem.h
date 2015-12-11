/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2015 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2015 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


/*
**    Include file for the local memory management.
*/

#ifndef __gc_hal_mem_h_
#define __gc_hal_mem_h_
#if (gcdENABLE_3D || gcdENABLE_VG)

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
**  Usage:

    The macros to declare MemPool type and functions are
    gcmMEM_DeclareFSMemPool (Type, TypeName, Prefix)
    gcmMEM_DeclareVSMemPool (Type, TypeName, Prefix)
    gcmMEM_DeclareAFSMemPool(Type, TypeName, Prefix)

    The data structures for MemPool are
    typedef struct _gcsMEM_FS_MEM_POOL *    gcsMEM_FS_MEM_POOL;
    typedef struct _gcsMEM_VS_MEM_POOL *    gcsMEM_VS_MEM_POOL;
    typedef struct _gcsMEM_AFS_MEM_POOL *    gcsMEM_AFS_MEM_POOL;

    The MemPool constructor and destructor functions are
    gcfMEM_InitFSMemPool(gcsMEM_FS_MEM_POOL *, gcoOS, gctUINT, gctUINT);
    gcfMEM_FreeFSMemPool(gcsMEM_FS_MEM_POOL *);
    gcfMEM_InitVSMemPool(gcsMEM_VS_MEM_POOL *, gcoOS, gctUINT, gctBOOL);
    gcfMEM_FreeVSMemPool(gcsMEM_VS_MEM_POOL *);
    gcfMEM_InitAFSMemPool(gcsMEM_AFS_MEM_POOL *, gcoOS, gctUINT);
    gcfMEM_FreeAFSMemPool(gcsMEM_AFS_MEM_POOL *);

    FS:  for Fixed-Size data structures
    VS:  for Variable-size data structures
    AFS: for Array of Fixed-Size data structures


    // Example 1: For a fixed-size data structure, struct gcsNode.
    // It is used locally in a file, so the functions are static without prefix.
    // At top level, declear allocate and free functions.
    // The first argument is the data type.
    // The second armument is the short name used in the fuctions.
    gcmMEM_DeclareFSMemPool(struct gcsNode, Node, );

    // The previous macro creates two inline functions,
    // _AllocateNode and _FreeNode.

    // In function or struct
    gcsMEM_FS_MEM_POOL nodeMemPool;

    // In function,
    struct gcsNode * node;
    gceSTATUS status;

    // Before using the memory pool, initialize it.
    // The second argument is the gcoOS object.
    // The third argument is the number of data structures to allocate for each chunk.
    status = gcfMEM_InitFSMemPool(&nodeMemPool, os, 100, sizeof(struct gcsNode));
    ...

    // Allocate a node.
    status = _AllocateNode(nodeMemPool, &node);
    ...
    // Free a node.
    _FreeNode(nodeMemPool, node);

    // After using the memory pool, free it.
    gcfMEM_FreeFSMemPool(&nodeMemPool);


    // Example 2: For array of fixed-size data structures, struct gcsNode.
    // It is used in several files, so the functions are extern with prefix.
    // At top level, declear allocate and free functions.
    // The first argument is the data type, and the second one is the short name
    // used in the fuctions.
    gcmMEM_DeclareAFSMemPool(struct gcsNode, NodeArray, gcfOpt);

    // The previous macro creates two inline functions,
    // gcfOpt_AllocateNodeArray and gcfOpt_FreeNodeArray.

    // In function or struct
    gcsMEM_AFS_MEM_POOL nodeArrayMemPool;

    // In function,
    struct gcsNode * nodeArray;
    gceSTATUS status;

    // Before using the array memory pool, initialize it.
    // The second argument is the gcoOS object, the third is the number of data
    // structures to allocate for each chunk.
    status = gcfMEM_InitAFSMemPool(&nodeArrayMemPool, os, sizeof(struct gcsNode));
    ...

    // Allocate a node array of size 100.
    status = gcfOpt_AllocateNodeArray(nodeArrayMemPool, &nodeArray, 100);
    ...
    // Free a node array.
    gcfOpt_FreeNodeArray(&nodeArrayMemPool, nodeArray);

    // After using the array memory pool, free it.
    gcfMEM_FreeAFSMemPool(&nodeArrayMemPool);

*******************************************************************************/

/*******************************************************************************
**    To switch back to use gcoOS_Allocate and gcoOS_Free, add
**    #define USE_LOCAL_MEMORY_POOL 0
**    before including this file.
*******************************************************************************/
#ifndef USE_LOCAL_MEMORY_POOL
/*
    USE_LOCAL_MEMORY_POOL

    This define enables the local memory management to improve performance.
*/
#define USE_LOCAL_MEMORY_POOL        1
#endif

/*******************************************************************************
**                            Memory Pool Data Structures
*******************************************************************************/
#if USE_LOCAL_MEMORY_POOL
    typedef struct _gcsMEM_FS_MEM_POOL *    gcsMEM_FS_MEM_POOL;
    typedef struct _gcsMEM_VS_MEM_POOL *    gcsMEM_VS_MEM_POOL;
    typedef struct _gcsMEM_AFS_MEM_POOL *    gcsMEM_AFS_MEM_POOL;
#else
    typedef gcoOS    gcsMEM_FS_MEM_POOL;
    typedef gcoOS    gcsMEM_VS_MEM_POOL;
    typedef gcoOS    gcsMEM_AFS_MEM_POOL;
#endif

/*******************************************************************************
**                            Memory Pool Macros
*******************************************************************************/
#if USE_LOCAL_MEMORY_POOL
#define gcmMEM_DeclareFSMemPool(Type, TypeName, Prefix) \
gceSTATUS \
Prefix##_Allocate##TypeName( \
    gcsMEM_FS_MEM_POOL        MemPool, \
    Type **                    Pointer \
    ) \
{ \
    return(gcfMEM_FSMemPoolGetANode(MemPool, (gctPOINTER *) Pointer)); \
} \
 \
gceSTATUS \
Prefix##_CAllocate##TypeName( \
    gcsMEM_FS_MEM_POOL        MemPool, \
    Type **                    Pointer \
    ) \
{ \
    gceSTATUS                status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x", MemPool, Pointer); \
    gcmERR_RETURN(gcfMEM_FSMemPoolGetANode(MemPool, (gctPOINTER *) Pointer)); \
    gcoOS_ZeroMemory(*(gctPOINTER *) Pointer, gcmSIZEOF(Type)); \
    gcmFOOTER(); \
    return gcvSTATUS_OK; \
} \
 \
gceSTATUS \
Prefix##_Free##TypeName( \
    gcsMEM_FS_MEM_POOL        MemPool, \
    Type *                    Pointer \
    ) \
{ \
    gceSTATUS                status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x", MemPool, Pointer); \
    status = gcfMEM_FSMemPoolFreeANode(MemPool, (gctPOINTER) Pointer); \
    gcmFOOTER(); \
    return status; \
} \
 \
gceSTATUS \
Prefix##_Free##TypeName##List( \
    gcsMEM_FS_MEM_POOL        MemPool, \
    Type *                    FirstPointer, \
    Type *                    LastPointer \
    ) \
{ \
    gceSTATUS               status; \
    gcmHEADER_ARG("MemPool=0x%x FirstPointer=0x%x LastPointer=0x%x", MemPool, FirstPointer, LastPointer); \
    status = gcfMEM_FSMemPoolFreeAList(MemPool, (gctPOINTER) FirstPointer, (gctPOINTER) LastPointer); \
    gcmFOOTER(); \
    return status; \
}

#define gcmMEM_DeclareVSMemPool(Type, TypeName, Prefix) \
gceSTATUS \
Prefix##_Allocate##TypeName( \
    gcsMEM_FS_MEM_POOL        MemPool, \
    Type **                    Pointer, \
    gctUINT                    Size \
    ) \
{ \
    gceSTATUS               status;\
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x Size=%u", MemPool, Pointer, Size); \
    status = gcfMEM_VSMemPoolGetANode(MemPool, Size, (gctPOINTER *) Pointer); \
    gcmFOOTER(); \
    return status; \
} \
 \
gceSTATUS \
 Prefix##_CAllocate##TypeName( \
    gcsMEM_FS_MEM_POOL        MemPool, \
    Type **                    Pointer, \
    gctUINT                    Size \
    ) \
{ \
    gceSTATUS                status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x Size=%u", MemPool, Pointer, Size); \
    gcmERR_RETURN(gcfMEM_VSMemPoolGetANode(MemPool, Size, (gctPOINTER *) Pointer)); \
    gcoOS_ZeroMemory(*(gctPOINTER *) Pointer, size); \
    gcmFOOTER(); \
    return gcvSTATUS_OK; \
} \
 \
gceSTATUS \
Prefix##_Free##TypeName( \
    gcsMEM_FS_MEM_POOL        MemPool, \
    Type *                    Pointer \
    ) \
{ \
    gceSTATUS               status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x", MemPool, Pinter); \
    status = gcfMEM_VSMemPoolFreeANode(MemPool, (gctPOINTER) Pointer); \
    gcmFOOTER(); \
    return status; \
}

#define gcmMEM_DeclareAFSMemPool(Type, TypeName, Prefix) \
gceSTATUS \
Prefix##_Allocate##TypeName( \
    gcsMEM_AFS_MEM_POOL        MemPool, \
    Type **                    Pointer, \
    gctUINT                    Count \
    ) \
{ \
    gceSTATUS               status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x Count=%u", MemPool, Pointer, Count); \
    status = gcfMEM_AFSMemPoolGetANode(MemPool, Count, (gctPOINTER *) Pointer); \
    gcmFOOTER(); \
    return status; \
} \
 \
gceSTATUS \
Prefix##_CAllocate##TypeName( \
    gcsMEM_AFS_MEM_POOL        MemPool, \
    Type **                    Pointer, \
    gctUINT                    Count \
    ) \
{ \
    gceSTATUS                status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x Count=%u", MemPool, Pointer, Count); \
    gcmERR_RETURN(gcfMEM_AFSMemPoolGetANode(MemPool, Count, (gctPOINTER *) Pointer)); \
    gcoOS_ZeroMemory(*(gctPOINTER *) Pointer, Count * gcmSIZEOF(Type)); \
    gcmFOOTER(); \
    return gcvSTATUS_OK; \
} \
 \
gceSTATUS \
Prefix##_Free##TypeName( \
    gcsMEM_AFS_MEM_POOL        MemPool, \
    Type *                    Pointer \
    ) \
{ \
    gceSTATUS               status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x", MemPool, Pointer); \
    status = gcfMEM_AFSMemPoolFreeANode(MemPool, (gctPOINTER) Pointer); \
    gcmFOOTER(); \
    return status; \
}

#else

#define gcmMEM_DeclareFSMemPool(Type, TypeName, Prefix) \
gceSTATUS \
Prefix##_Allocate##TypeName( \
    gcsMEM_FS_MEM_POOL        MemPool, \
    Type **                    Pointer \
    ) \
{ \
    gceSTATUS               status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x", MemPool, Pointer); \
    status = gcoOS_Allocate(MemPool, \
                            gcmSIZEOF(Type), \
                            (gctPOINTER *) Pointer); \
    gcmFOOTER(); \
    return status; \
} \
 \
gceSTATUS \
Prefix##_CAllocate##TypeName( \
    gcsMEM_FS_MEM_POOL        MemPool, \
    Type **                    Pointer \
    ) \
{ \
    gceSTATUS                status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x", MemPool, Pointer); \
    gcmERR_RETURN(gcoOS_Allocate(MemPool, \
                            gcmSIZEOF(Type), \
                            (gctPOINTER *) Pointer)); \
    gcoOS_ZeroMemory(*(gctPOINTER *) Pointer, gcmSIZEOF(Type)); \
    gcmFOOTER(); \
    return gcvSTATUS_OK; \
} \
 \
gceSTATUS \
Prefix##_Free##TypeName( \
    gcsMEM_FS_MEM_POOL        MemPool, \
    Type *                    Pointer \
    ) \
{ \
    gceSTATUS                status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x", MemPool, Pointer); \
    status = gcmOS_SAFE_FREE(MemPool, Pointer); \
    gcmFOOTER(); \
    return status; \
}

#define gcmMEM_DeclareVSMemPool(Type, TypeName, Prefix) \
gceSTATUS \
Prefix##_Allocate##TypeName( \
    gcsMEM_VS_MEM_POOL        MemPool, \
    Type **                    Pointer, \
    gctUINT                    Size \
    ) \
{ \
    gceSTATUS                status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x Size=%u", MemPool, Pointer, Size); \
    status = gcoOS_Allocate(MemPool, \
                            Size, \
                            (gctPOINTER *) Pointer); \
    gcmFOOTER(); \
    return status; \
} \
 \
gceSTATUS \
Prefix##_CAllocate##TypeName( \
    gcsMEM_VS_MEM_POOL        MemPool, \
    Type **                    Pointer, \
    gctUINT                    Size \
    ) \
{ \
    gceSTATUS                status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x Size=%u", MemPool, Pointer, Size); \
    gcmERR_RETURN(gcoOS_Allocate(MemPool, \
                            Size, \
                            (gctPOINTER *) Pointer)); \
    gcoOS_ZeroMemory(*(gctPOINTER *) Pointer, Size); \
    gcmFOOTER(); \
    return gcvSTATUS_OK; \
} \
 \
gceSTATUS \
Prefix##_Free##TypeName( \
    gcsMEM_VS_MEM_POOL        MemPool, \
    Type *                    Pointer \
    ) \
{ \
    gceSTATUS                status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x", MemPool, Pointer); \
    status = gcmOS_SAFE_FREE(MemPool, Pointer); \
    gcmFOOTER(); \
    return status; \
}

#define gcmMEM_DeclareAFSMemPool(Type, TypeName, Prefix) \
gceSTATUS \
Prefix##_Allocate##TypeName( \
    gcsMEM_AFS_MEM_POOL        MemPool, \
    Type **                    Pointer, \
    gctUINT                    Count \
    ) \
{ \
    gceSTATUS                status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x Count=%u", MemPool, Pointer, Count); \
    status = gcoOS_Allocate(MemPool, \
                            Count * gcmSIZEOF(Type), \
                            (gctPOINTER *) Pointer); \
    gcmFOOTER(); \
    return status; \
} \
 \
gceSTATUS \
Prefix##_CAllocate##TypeName( \
    gcsMEM_AFS_MEM_POOL        MemPool, \
    Type **                    Pointer, \
    gctUINT                    Count \
    ) \
{ \
    gceSTATUS                status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x Count=%u", MemPool, Pointer, Count); \
    gcmERR_RETURN(gcoOS_Allocate(MemPool, \
                            Count * gcmSIZEOF(Type), \
                            (gctPOINTER *) Pointer)); \
    gcoOS_ZeroMemory(*(gctPOINTER *) Pointer, Count * gcmSIZEOF(Type)); \
    gcmFOOTER(); \
    return gcvSTATUS_OK; \
} \
 \
gceSTATUS \
Prefix##_Free##TypeName( \
    gcsMEM_AFS_MEM_POOL        MemPool, \
    Type *                    Pointer \
    ) \
{ \
    gceSTATUS                status; \
    gcmHEADER_ARG("MemPool=0x%x Pointer=0x%x", MemPool, Pointer); \
    status = gcmOS_SAFE_FREE(MemPool, Pointer); \
    gcmFOOTER(); \
    return status; \
}
#endif

/*******************************************************************************
**                            Memory Pool Data Functions
*******************************************************************************/
gceSTATUS
gcfMEM_InitFSMemPool(
    IN gcsMEM_FS_MEM_POOL * MemPool,
    IN gcoOS                OS,
    IN gctUINT                NodeCount,
    IN gctUINT                NodeSize
    );

gceSTATUS
gcfMEM_FreeFSMemPool(
    IN gcsMEM_FS_MEM_POOL * MemPool
    );

gceSTATUS
gcfMEM_FSMemPoolGetANode(
    IN gcsMEM_FS_MEM_POOL    MemPool,
    OUT gctPOINTER *        Node
    );

gceSTATUS
gcfMEM_FSMemPoolFreeANode(
    IN gcsMEM_FS_MEM_POOL    MemPool,
    IN gctPOINTER            Node
    );

gceSTATUS
gcfMEM_FSMemPoolFreeAList(
    IN gcsMEM_FS_MEM_POOL    MemPool,
    IN gctPOINTER            FirstNode,
    IN gctPOINTER            LastNode
    );

gceSTATUS
gcfMEM_InitVSMemPool(
    IN gcsMEM_VS_MEM_POOL * MemPool,
    IN gcoOS                OS,
    IN gctUINT                BlockSize,
    IN gctBOOL                RecycleFreeNode
    );

gceSTATUS
gcfMEM_FreeVSMemPool(
    IN gcsMEM_VS_MEM_POOL * MemPool
    );

gceSTATUS
gcfMEM_VSMemPoolGetANode(
    IN gcsMEM_VS_MEM_POOL    MemPool,
    IN gctUINT                Size,
    IN gctUINT                Alignment,
    OUT gctPOINTER *        Node
    );

gceSTATUS
gcfMEM_VSMemPoolFreeANode(
    IN gcsMEM_VS_MEM_POOL    MemPool,
    IN gctPOINTER            Node
    );

gceSTATUS
gcfMEM_InitAFSMemPool(
    IN gcsMEM_AFS_MEM_POOL *MemPool,
    IN gcoOS                OS,
    IN gctUINT                NodeCount,
    IN gctUINT                NodeSize
    );

gceSTATUS
gcfMEM_FreeAFSMemPool(
    IN gcsMEM_AFS_MEM_POOL *MemPool
    );

gceSTATUS
gcfMEM_AFSMemPoolGetANode(
    IN gcsMEM_AFS_MEM_POOL    MemPool,
    IN gctUINT                Count,
    OUT gctPOINTER *        Node
    );

gceSTATUS
gcfMEM_AFSMemPoolFreeANode(
    IN gcsMEM_AFS_MEM_POOL    MemPool,
    IN gctPOINTER            Node
    );

#ifdef __cplusplus
}
#endif

#endif /* (gcdENABLE_3D || gcdENABLE_VG) */
#endif /* __gc_hal_mem_h_ */

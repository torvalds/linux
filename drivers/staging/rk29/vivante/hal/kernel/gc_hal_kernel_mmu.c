/****************************************************************************
*  
*    Copyright (C) 2005 - 2011 by Vivante Corp.
*  
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*  
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*  
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*  
*****************************************************************************/




#include "gc_hal_kernel_precomp.h"

#define _GC_OBJ_ZONE	gcvZONE_MMU

#if gcdENABLE_MMU_PROTECTING
#include "../os/linux/kernel/gc_hal_kernel_os.h"
#endif

typedef enum _gceMMU_TYPE
{
	gcvMMU_USED = 0,
	gcvMMU_SINGLE,
	gcvMMU_FREE,
}
gceMMU_TYPE;

static gceSTATUS
_Link(
	IN gckMMU Mmu,
	IN gctUINT32 Index,
	IN gctUINT32 Next
	)
{
	if (Index >= Mmu->pageTableEntries)
	{
		/* Just move heap pointer. */
		Mmu->heapList = Next;
	}
	else
	{
		/* Address page table. */
		gctUINT32_PTR pageTable = Mmu->pageTableLogical;

		/* Dispatch on node type. */
		switch (pageTable[Index] & 0xFF)
		{
		case gcvMMU_SINGLE:
			/* Set single index. */
			pageTable[Index] = (Next << 8) | gcvMMU_SINGLE;
			break;

		case gcvMMU_FREE:
			/* Set index. */
			pageTable[Index + 1] = Next;
			break;

		default:
			gcmkFATAL("MMU table correcupted at index %u!", Index);
			return gcvSTATUS_HEAP_CORRUPTED;
		}
	}

	/* Success. */
	return gcvSTATUS_OK;
}

static gceSTATUS
_AddFree(
	IN gckMMU Mmu, 
	IN gctUINT32 Index, 
	IN gctUINT32 Node, 
	IN gctUINT32 Count
	)
{
	gctUINT32_PTR pageTable = Mmu->pageTableLogical;

	if (Count == 1)
	{
		/* Initialize a single page node. */
		pageTable[Node] = (~0U << 8) | gcvMMU_SINGLE;
	}
	else
	{
		/* Initialize the node. */
		pageTable[Node + 0] = (Count << 8) | gcvMMU_FREE;
		pageTable[Node + 1] = ~0U;
	}

	/* Append the node. */
	return _Link(Mmu, Index, Node);
}

static gceSTATUS
_Collect(
	IN gckMMU Mmu
	)
{
	gctUINT32_PTR pageTable = Mmu->pageTableLogical;
	gceSTATUS status;
	gctUINT32 i, previous, start = 0, count = 0;

	/* Flush the MMU cache. */
	gcmkONERROR(
		gckHARDWARE_FlushMMU(Mmu->hardware));

	previous = Mmu->heapList = ~0U;
	Mmu->freeNodes = gcvFALSE;

	/* Walk the entire page table. */
	for (i = 0; i < Mmu->pageTableEntries; ++i)
	{
		/* Dispatch based on type of page. */
		switch (pageTable[i] & 0xFF)
		{
		case gcvMMU_USED:
			/* Used page, so close any open node. */
			if (count > 0)
			{
				/* Add the node. */
				gcmkONERROR(_AddFree(Mmu, previous, start, count));

				/* Reset the node. */
				previous = start;
				count    = 0;
			}
			break;

		case gcvMMU_SINGLE:
			/* Single free node. */
			if (count++ == 0)
			{
				/* Start a new node. */
				start = i;
			}
			break;

		case gcvMMU_FREE:
			/* A free node. */
			if (count == 0)
			{
				/* Start a new node. */
				start = i;
			}

			/* Advance the count. */
			count += pageTable[i] >> 8;

			/* Advance the index into the page table. */
			i     += (pageTable[i] >> 8) - 1;
			break;

		default:
			gcmkFATAL("MMU page table correcupted at index %u!", i);
			return gcvSTATUS_HEAP_CORRUPTED;
		}
	}

	/* See if we have an open node left. */
	if (count > 0)
	{
		/* Add the node to the list. */
		gcmkONERROR(_AddFree(Mmu, previous, start, count));
	}

	gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_MMU,
				   "Performed a garbage collection of the MMU heap.");

	/* Success. */
	return gcvSTATUS_OK;

OnError:
	/* Return the staus. */
	return status;
}

/*******************************************************************************
**
**	gckMMU_Construct
**
**	Construct a new gckMMU object.
**
**	INPUT:
**
**		gckKERNEL Kernel
**			Pointer to an gckKERNEL object.
**
**		gctSIZE_T MmuSize
**			Number of bytes for the page table.
**
**	OUTPUT:
**
**		gckMMU * Mmu
**			Pointer to a variable that receives the gckMMU object pointer.
*/
gceSTATUS 
gckMMU_Construct(
	IN gckKERNEL Kernel,
	IN gctSIZE_T MmuSize,
	OUT gckMMU * Mmu
	)
{
	gckOS os;
	gckHARDWARE hardware;
	gceSTATUS status;
	gckMMU mmu = gcvNULL;
	gctUINT32_PTR pageTable;
	
#if gcdENABLE_MMU_PROTECTING
	gctSIZE_T pageTableSize_x2;
	gctSIZE_T OnePage = PAGE_SIZE;
#endif

	gcmkHEADER_ARG("Kernel=0x%x MmuSize=%lu", Kernel, MmuSize);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
	gcmkVERIFY_ARGUMENT(MmuSize > 0);
	gcmkVERIFY_ARGUMENT(Mmu != gcvNULL);

	/* Extract the gckOS object pointer. */
	os = Kernel->os;
	gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

	/* Extract the gckHARDWARE object pointer. */
	hardware = Kernel->hardware;
	gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

	/* Allocate memory for the gckMMU object. */
	gcmkONERROR(
		gckOS_Allocate(os, sizeof(struct _gckMMU), (gctPOINTER *) &mmu));

	/* Initialize the gckMMU object. */
	mmu->object.type      = gcvOBJ_MMU;
	mmu->os               = os;
	mmu->hardware         = hardware;
	mmu->pageTableMutex   = gcvNULL;
	mmu->pageTableLogical = gcvNULL;
#ifdef __QNXNTO__
	mmu->nodeList         = gcvNULL;
	mmu->nodeMutex		  = gcvNULL;
#endif

	/* Create the page table mutex. */
	gcmkONERROR(gckOS_CreateMutex(os, &mmu->pageTableMutex));

#ifdef __QNXNTO__
	/* Create the node list mutex. */
	gcmkONERROR(gckOS_CreateMutex(os, &mmu->nodeMutex));
#endif

	/* Allocate the page table (not more than 256 kB). */
	mmu->pageTableSize = gcmMIN(MmuSize, 256 << 10);

	
#if gcdENABLE_MMU_PROTECTING 
	//one pageTableSize to store the labels ,another pageTableSize to store the physical addresses
	pageTableSize_x2 = mmu->pageTableSize*2;
	gcmkONERROR(
		gckOS_AllocateContiguous(os,
								 gcvFALSE,
								 &pageTableSize_x2,
								 &mmu->pageTablePhysical,
								 (gctPOINTER *) &mmu->pageTableLogical));
	
	if(pageTableSize_x2 != mmu->pageTableSize*2)
	{
		printk("pageTableSize_x2 != mmu->pageTableSize*2\n");
		goto OnError;
	}

	//this page's physical address is set to the freed pageTable in gckMMU_FreePages
	gcmkONERROR(
		gckOS_AllocateContiguous(os,
								 gcvFALSE,
								 &OnePage,
								 &mmu->FreePagePhysical,
								 (gctPOINTER *)&mmu->FreePageLogical));
#else
	gcmkONERROR(
		gckOS_AllocateContiguous(os,
								 gcvFALSE,
								 &mmu->pageTableSize,
								 &mmu->pageTablePhysical,
								 (gctPOINTER *) &mmu->pageTableLogical));
#endif

	/* Compute number of entries in page table. */
	mmu->pageTableEntries = mmu->pageTableSize / sizeof(gctUINT32);

	/* Mark all pages as free. */
	pageTable      = mmu->pageTableLogical;
	pageTable[0]   = (mmu->pageTableEntries << 8) | gcvMMU_FREE;
	pageTable[1]   = ~0U;
	mmu->heapList  = 0;
	mmu->freeNodes = gcvFALSE;

	/* Set page table address. */
#if gcdENABLE_MMU_PROTECTING
	//logical address added pageTableEntries to store the the physical addresses
	gcmkONERROR(
		gckHARDWARE_SetMMU(hardware, (gctPOINTER) (mmu->pageTableLogical + mmu->pageTableEntries)));
#else
	gcmkONERROR(
		gckHARDWARE_SetMMU(hardware, (gctPOINTER) mmu->pageTableLogical));
#endif

	/* Return the gckMMU object pointer. */
	*Mmu = mmu;

	/* Success. */
	gcmkFOOTER_ARG("*Mmu=0x%x", *Mmu);
	return gcvSTATUS_OK;

OnError:
	/* Roll back. */
	if (mmu != gcvNULL)
	{
		if (mmu->pageTableLogical != gcvNULL)
		{
			/* Free the page table. */
#if gcdENABLE_MMU_PROTECTING
			gcmkVERIFY_OK(
				gckOS_FreeContiguous(os, 
									 mmu->pageTablePhysical,
									 (gctPOINTER) mmu->pageTableLogical,
									 mmu->pageTableSize*2));

			gcmkVERIFY_OK(
				gckOS_FreeContiguous(os, 
									 mmu->FreePagePhysical,
									 (gctPOINTER) mmu->FreePageLogical,
									 PAGE_SIZE));

#else
			gcmkVERIFY_OK(
				gckOS_FreeContiguous(os, 
									 mmu->pageTablePhysical,
									 (gctPOINTER) mmu->pageTableLogical,
									 mmu->pageTableSize));
#endif 
		}

		if (mmu->pageTableMutex != gcvNULL)
		{
			/* Delete the mutex. */
			gcmkVERIFY_OK(
				gckOS_DeleteMutex(os, mmu->pageTableMutex));
		}

#ifdef __QNXNTO__		
		if (mmu->nodeMutex != gcvNULL)
		{
			/* Delete the mutex. */
			gcmkVERIFY_OK(
				gckOS_DeleteMutex(os, mmu->nodeMutex));
		}
#endif

		/* Mark the gckMMU object as unknown. */
		mmu->object.type = gcvOBJ_UNKNOWN;

		/* Free the allocates memory. */
		gcmkVERIFY_OK(gckOS_Free(os, mmu));
	}

	/* Return the status. */
	gcmkFOOTER();
	return status;
}

/*******************************************************************************
**
**	gckMMU_Destroy
**
**	Destroy a gckMMU object.
**
**	INPUT:
**
**		gckMMU Mmu
**			Pointer to an gckMMU object.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS 
gckMMU_Destroy(
	IN gckMMU Mmu
	)
{
#ifdef __QNXNTO__
    gcuVIDMEM_NODE_PTR node, next;
#endif

	gcmkHEADER_ARG("Mmu=0x%x", Mmu);
	
	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);

#ifdef __QNXNTO__
	/* Free all associated virtual memory. */
	for (node = Mmu->nodeList; node != gcvNULL; node = next)
	{
		next = node->Virtual.next;
	    gcmkVERIFY_OK(gckVIDMEM_Free(node, gcvNULL));
	}
#endif

	/* Free the page table. */
#if gcdENABLE_MMU_PROTECTING
	gcmkVERIFY_OK(
		gckOS_FreeContiguous(Mmu->os,
							 Mmu->pageTablePhysical,
							 (gctPOINTER) Mmu->pageTableLogical,
							 Mmu->pageTableSize*2));
	gcmkVERIFY_OK(
		gckOS_FreeContiguous(Mmu->os,
							 Mmu->FreePagePhysical,
							 (gctPOINTER) Mmu->FreePageLogical,
							 PAGE_SIZE));
#else
	gcmkVERIFY_OK(
		gckOS_FreeContiguous(Mmu->os,
							 Mmu->pageTablePhysical,
							 (gctPOINTER) Mmu->pageTableLogical,
							 Mmu->pageTableSize));
#endif

#ifdef __QNXNTO__
	/* Delete the node list mutex. */
	gcmkVERIFY_OK(gckOS_DeleteMutex(Mmu->os, Mmu->nodeMutex));
#endif

	/* Delete the page table mutex. */
	gcmkVERIFY_OK(gckOS_DeleteMutex(Mmu->os, Mmu->pageTableMutex));

	/* Mark the gckMMU object as unknown. */
	Mmu->object.type = gcvOBJ_UNKNOWN;

	/* Free the gckMMU object. */
	gcmkVERIFY_OK(gckOS_Free(Mmu->os, Mmu));

	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;
}

/*******************************************************************************
**
**	gckMMU_AllocatePages
**
**	Allocate pages inside the page table.
**
**	INPUT:
**
**		gckMMU Mmu
**			Pointer to an gckMMU object.
**
**		gctSIZE_T PageCount
**			Number of pages to allocate.
**
**	OUTPUT:
**
**		gctPOINTER * PageTable
**			Pointer to a variable that receives the	base address of the page
**			table.
**
**		gctUINT32 * Address
**			Pointer to a variable that receives the hardware specific address.
*/
gceSTATUS
gckMMU_AllocatePages(
	IN gckMMU Mmu,
	IN gctSIZE_T PageCount,
	OUT gctPOINTER * PageTable,
	OUT gctUINT32 * Address
	)
{
	gceSTATUS status;
	gctBOOL mutex = gcvFALSE;
	gctUINT32 index = 0, previous = ~0U, left;
	gctUINT32_PTR pageTable;
	gctBOOL gotIt;
	gctUINT32 address;
	
	gcmkHEADER_ARG("Mmu=0x%x PageCount=%lu", Mmu, PageCount);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);
	gcmkVERIFY_ARGUMENT(PageCount > 0);
	gcmkVERIFY_ARGUMENT(PageTable != gcvNULL);

	if (PageCount > Mmu->pageTableEntries)
	{
		/* Not enough pages avaiable. */
		gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
	}

	/* Grab the mutex. */
	gcmkONERROR(gckOS_AcquireMutex(Mmu->os, Mmu->pageTableMutex, gcvINFINITE));
	mutex = gcvTRUE;

	/* Cast pointer to page table. */
	for (pageTable = Mmu->pageTableLogical, gotIt = gcvFALSE; !gotIt;)
	{
		/* Walk the heap list. */
		for (index = Mmu->heapList; !gotIt && (index < Mmu->pageTableEntries);)
		{
			/* Check the node type. */
			switch (pageTable[index] & 0xFF)
			{
			case gcvMMU_SINGLE:
				/* Single odes are valid if we only need 1 page. */
				if (PageCount == 1)
				{
					gotIt = gcvTRUE;
				}
				else
				{
					/* Move to next node. */
					previous = index;
					index    = pageTable[index] >> 8;
				}
				break;

			case gcvMMU_FREE:
				/* Test if the node has enough space. */
				if (PageCount <= (pageTable[index] >> 8))
				{
					gotIt = gcvTRUE;
				}
				else
				{
					/* Move to next node. */
					previous = index;
					index    = pageTable[index + 1];
				}
				break;

			default:
				gcmkFATAL("MMU table correcupted at index %u!", index);
				gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
			}
		}

		/* Test if we are out of memory. */
		if (index >= Mmu->pageTableEntries)
		{
			if (Mmu->freeNodes)
			{
				/* Time to move out the trash! */
				gcmkONERROR(_Collect(Mmu));
			}
			else
			{
				/* Out of resources. */
				gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
			}
		}
	}

	switch (pageTable[index] & 0xFF)
	{
	case gcvMMU_SINGLE:
		/* Unlink single node from free list. */
		gcmkONERROR(
			_Link(Mmu, previous, pageTable[index] >> 8));
		break;

	case gcvMMU_FREE:
		/* Check how many pages will be left. */
		left = (pageTable[index] >> 8) - PageCount;
		switch (left)
		{
		case 0:
			/* The entire node is consumed, just unlink it. */
			gcmkONERROR(
				_Link(Mmu, previous, pageTable[index + 1]));
			break;

		case 1:
			/* One page will remain.  Convert the node to a single node and
			** advance the index. */
			pageTable[index] = (pageTable[index + 1] << 8) | gcvMMU_SINGLE;
			index ++;
			break;

		default:
			/* Enough pages remain for a new node.  However, we will just adjust
			** the size of the current node and advance the index. */
			pageTable[index] = (left << 8) | gcvMMU_FREE;
			index += left;
			break;
		}
		break;
	}

	/* Mark node as used. */
	pageTable[index] = gcvMMU_USED;

	/* Return pointer to page table. */
#if gcdENABLE_MMU_PROTECTING
	//index add pageTableEntries to store real physical address 
	*PageTable = &pageTable[index +Mmu->pageTableEntries];

	//must do memset because the elements may be set to ~0U in gckMMU_FreePages or _AddFree
	memset(&pageTable[index], gcvMMU_USED, PageCount*4);
#else
	*PageTable = &pageTable[index];
#endif

	/* Build virtual address. */
	gcmkONERROR(
		gckHARDWARE_BuildVirtualAddress(Mmu->hardware, index, 0, &address));

	if (Address != gcvNULL)
	{
		*Address = address;
	}

	/* Release the mutex. */
	gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->pageTableMutex));

	/* Success. */
	gcmkFOOTER_ARG("*PageTable=0x%x *Address=%08x",
				   *PageTable, gcmOPT_VALUE(Address));
	return gcvSTATUS_OK;

OnError:
	if (mutex)
	{
		/* Release the mutex. */
		gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->pageTableMutex));
	}

	/* Return the status. */
	gcmkFOOTER();
	return status;
}

/*******************************************************************************
**
**	gckMMU_FreePages
**
**	Free pages inside the page table.
**
**	INPUT:
**
**		gckMMU Mmu
**			Pointer to an gckMMU object.
**
**		gctPOINTER PageTable
**			Base address of the page table to free.
**
**		gctSIZE_T PageCount
**			Number of pages to free.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS 
gckMMU_FreePages(
	IN gckMMU Mmu,
	IN gctPOINTER PageTable,
	IN gctSIZE_T PageCount
	)
{
	gctUINT32_PTR pageTable;

#if gcdENABLE_MMU_PROTECTING
	int i;
#endif

	gcmkHEADER_ARG("Mmu=0x%x PageTable=0x%x PageCount=%lu",
				   Mmu, PageTable, PageCount);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);
	gcmkVERIFY_ARGUMENT(PageTable != gcvNULL);
	gcmkVERIFY_ARGUMENT(PageCount > 0);

	/* Convert the pointer. */
	pageTable = (gctUINT32_PTR) PageTable;

#if gcdENABLE_MMU_PROTECTING
	//sub pageTableEntries to get the address that store lables
	pageTable -= Mmu->pageTableEntries;
#endif

	if (PageCount == 1)
	{
		/* Single page node. */
		pageTable[0] = (~0U << 8) | gcvMMU_SINGLE;
		
#if gcdENABLE_MMU_PROTECTING
		//set the special phsical address to avoid GPU accessing other addresses 
		pageTable[Mmu->pageTableEntries] =  ((PLINUX_MDL)(Mmu->FreePagePhysical))->dmaHandle;
#endif 

	}
	else
	{
		/* Mark the node as free. */
		pageTable[0] = (PageCount << 8) | gcvMMU_FREE;
		pageTable[1] = ~0U;
		
#if gcdENABLE_MMU_PROTECTING
		//set the special phsical address to avoid GPU accessing other addresses 
		for(i=0; i<PageCount; i++)
		{
			pageTable[Mmu->pageTableEntries+i] = ((PLINUX_MDL)(Mmu->FreePagePhysical))->dmaHandle;
		}
#endif 

	}
	
	/* We have free nodes. */
	Mmu->freeNodes = gcvTRUE;
	
	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;
}

#ifdef __QNXNTO__
gceSTATUS
gckMMU_InsertNode(
    IN gckMMU Mmu,
    IN gcuVIDMEM_NODE_PTR Node)
{
    gceSTATUS status;
    gctBOOL mutex = gcvFALSE;

    gcmkHEADER_ARG("Mmu=0x%x Node=0x%x", Mmu, Node);

    gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);

    gcmkONERROR(gckOS_AcquireMutex(Mmu->os, Mmu->nodeMutex, gcvINFINITE));
    mutex = gcvTRUE;

    Node->Virtual.next = Mmu->nodeList;
    Mmu->nodeList = Node;

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->nodeMutex));

    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    if (mutex)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->nodeMutex));
    }

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckMMU_RemoveNode(
    IN gckMMU Mmu,
    IN gcuVIDMEM_NODE_PTR Node)
{
    gceSTATUS status;
    gctBOOL mutex = gcvFALSE;
    gcuVIDMEM_NODE_PTR *iter;

    gcmkHEADER_ARG("Mmu=0x%x Node=0x%x", Mmu, Node);

    gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);

    gcmkONERROR(gckOS_AcquireMutex(Mmu->os, Mmu->nodeMutex, gcvINFINITE));
    mutex = gcvTRUE;

	for (iter = &Mmu->nodeList; *iter; iter = &(*iter)->Virtual.next)
	{
		if (*iter == Node)
		{
			*iter = Node->Virtual.next;
			break;
		}
	}

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->nodeMutex));

    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    if (mutex)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->nodeMutex));
    }

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckMMU_FreeHandleMemory(
    IN gckMMU Mmu,
    IN gctHANDLE Handle
    )
{
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;
    gcuVIDMEM_NODE_PTR curr, next;

    gcmkHEADER_ARG("Mmu=0x%x Handle=0x%x", Mmu, Handle);

    gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);

    gcmkONERROR(gckOS_AcquireMutex(Mmu->os, Mmu->nodeMutex, gcvINFINITE));
    acquired = gcvTRUE;

    for (curr = Mmu->nodeList; curr != gcvNULL; curr = next)
    {
        next = curr->Virtual.next;

        if (curr->Virtual.handle == Handle)
        {
            while (curr->Virtual.locked > 0 || curr->Virtual.unlockPending)
            {
                gcmkONERROR(gckVIDMEM_Unlock(curr, gcvSURF_TYPE_UNKNOWN, gcvNULL, gcvNULL));
            }

            gcmkVERIFY_OK(gckVIDMEM_Free(curr, gcvNULL));
        }
    }

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->nodeMutex));

    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->nodeMutex));
    }

    gcmkFOOTER();
    return status;
}
#endif

/******************************************************************************
****************************** T E S T   C O D E ******************************
******************************************************************************/


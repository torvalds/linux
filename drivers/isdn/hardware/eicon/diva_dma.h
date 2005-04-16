
/*
 *
  Copyright (c) Eicon Networks, 2002.
 *
  This source file is supplied for the use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    2.1
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef __DIVA_DMA_MAPPING_IFC_H__
#define __DIVA_DMA_MAPPING_IFC_H__
typedef struct _diva_dma_map_entry  diva_dma_map_entry_t;
struct _diva_dma_map_entry* diva_alloc_dma_map (void* os_context, int nentries);
void diva_init_dma_map_entry (struct _diva_dma_map_entry* pmap,
                              int nr, void* virt, dword phys,
                              void* addr_handle);
int diva_alloc_dma_map_entry (struct _diva_dma_map_entry* pmap);
void diva_free_dma_map_entry (struct _diva_dma_map_entry* pmap, int entry);
void diva_get_dma_map_entry (struct _diva_dma_map_entry* pmap, int nr,
                             void** pvirt, dword* pphys);
void diva_free_dma_mapping (struct _diva_dma_map_entry* pmap);
/*
  Functionality to be implemented by OS wrapper
  and running in process context
  */
void diva_init_dma_map (void* hdev,
                        struct _diva_dma_map_entry** ppmap,
                        int nentries);
void diva_free_dma_map (void* hdev,
                        struct _diva_dma_map_entry* pmap);
void* diva_get_entry_handle (struct _diva_dma_map_entry* pmap, int nr);
#endif

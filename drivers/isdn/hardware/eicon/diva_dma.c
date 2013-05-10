
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
#include "platform.h"
#include "diva_dma.h"
/*
  Every entry has length of PAGE_SIZE
  and represents one single physical page
  */
struct _diva_dma_map_entry {
  int   busy;
  dword phys_bus_addr;  /* 32bit address as seen by the card */
  void* local_ram_addr; /* local address as seen by the host */
  void* addr_handle;    /* handle uset to free allocated memory */
};
/*
  Create local mapping structure and init it to default state
  */
struct _diva_dma_map_entry* diva_alloc_dma_map (void* os_context, int nentries) {
  diva_dma_map_entry_t* pmap = diva_os_malloc(0, sizeof(*pmap)*(nentries+1));
  if (pmap)
	  memset (pmap, 0, sizeof(*pmap)*(nentries+1));
  return pmap;
}
/*
  Free local map (context should be freed before) if any
  */
void diva_free_dma_mapping (struct _diva_dma_map_entry* pmap) {
  if (pmap) {
    diva_os_free (0, pmap);
  }
}
/*
  Set information saved on the map entry
  */
void diva_init_dma_map_entry (struct _diva_dma_map_entry* pmap,
                              int nr, void* virt, dword phys,
                              void* addr_handle) {
  pmap[nr].phys_bus_addr  = phys;
  pmap[nr].local_ram_addr = virt;
  pmap[nr].addr_handle    = addr_handle;
}
/*
  Allocate one single entry in the map
  */
int diva_alloc_dma_map_entry (struct _diva_dma_map_entry* pmap) {
  int i;
  for (i = 0; (pmap && pmap[i].local_ram_addr); i++) {
    if (!pmap[i].busy) {
      pmap[i].busy = 1;
      return (i);
    }
  }
  return (-1);
}
/*
  Free one single entry in the map
  */
void diva_free_dma_map_entry (struct _diva_dma_map_entry* pmap, int nr) {
  pmap[nr].busy = 0;
}
/*
  Get information saved on the map entry
  */
void diva_get_dma_map_entry (struct _diva_dma_map_entry* pmap, int nr,
                             void** pvirt, dword* pphys) {
  *pphys = pmap[nr].phys_bus_addr;
  *pvirt = pmap[nr].local_ram_addr;
}
void* diva_get_entry_handle (struct _diva_dma_map_entry* pmap, int nr) {
  return (pmap[nr].addr_handle);
}

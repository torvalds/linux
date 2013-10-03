/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Copyright (c) 2005 Linas Vepstas <linas@linas.org>
 */

#ifndef ASM_POWERPC_EEH_EVENT_H
#define ASM_POWERPC_EEH_EVENT_H
#ifdef __KERNEL__

/*
 * structure holding pci controller data that describes a
 * change in the isolation status of a PCI slot.  A pointer
 * to this struct is passed as the data pointer in a notify
 * callback.
 */
struct eeh_event {
	struct list_head	list;	/* to form event queue	*/
	struct eeh_pe		*pe;	/* EEH PE		*/
};

int eeh_event_init(void);
int eeh_send_failure_event(struct eeh_pe *pe);
void eeh_remove_event(struct eeh_pe *pe);
void eeh_handle_event(struct eeh_pe *pe);

#endif /* __KERNEL__ */
#endif /* ASM_POWERPC_EEH_EVENT_H */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright IBM Corp. 2015
 *
 * Authors: Gavin Shan <gwshan@linux.vnet.ibm.com>
 */

#ifndef _ASM_POWERPC_EEH_H
#define _ASM_POWERPC_EEH_H

/* PE states */
#define EEH_PE_STATE_NORMAL		0	/* Normal state		*/
#define EEH_PE_STATE_RESET		1	/* PE reset asserted	*/
#define EEH_PE_STATE_STOPPED_IO_DMA	2	/* Frozen PE		*/
#define EEH_PE_STATE_STOPPED_DMA	4	/* Stopped DMA only	*/
#define EEH_PE_STATE_UNAVAIL		5	/* Unavailable		*/

#endif /* _ASM_POWERPC_EEH_H */

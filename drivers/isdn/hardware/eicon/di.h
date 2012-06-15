
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
/*
 *  some macros for detailed trace management
 */
#include "di_dbg.h"
/*****************************************************************************/
#define XMOREC 0x1f
#define XMOREF 0x20
#define XBUSY  0x40
#define RMORE  0x80
#define DIVA_MISC_FLAGS_REMOVE_PENDING    0x01
#define DIVA_MISC_FLAGS_NO_RC_CANCELLING  0x02
#define DIVA_MISC_FLAGS_RX_DMA            0x04
/* structure for all information we have to keep on a per   */
/* adapater basis                                           */
typedef struct adapter_s ADAPTER;
struct adapter_s {
	void *io;
	byte IdTable[256];
	byte IdTypeTable[256];
	byte FlowControlIdTable[256];
	byte FlowControlSkipTable[256];
	byte ReadyInt;
	byte RcExtensionSupported;
	byte misc_flags_table[256];
	dword protocol_capabilities;
	byte (*ram_in)(ADAPTER *a, void *adr);
	word (*ram_inw)(ADAPTER *a, void *adr);
	void (*ram_in_buffer)(ADAPTER *a, void *adr, void *P, word length);
	void (*ram_look_ahead)(ADAPTER *a, PBUFFER *RBuffer, ENTITY *e);
	void (*ram_out)(ADAPTER *a, void *adr, byte data);
	void (*ram_outw)(ADAPTER *a, void *adr, word data);
	void (*ram_out_buffer)(ADAPTER *a, void *adr, void *P, word length);
	void (*ram_inc)(ADAPTER *a, void *adr);
#if defined(DIVA_ISTREAM)
	dword rx_stream[256];
	dword tx_stream[256];
	word tx_pos[256];
	word rx_pos[256];
	byte stream_buffer[2512];
	dword (*ram_offset)(ADAPTER *a);
	void (*ram_out_dw)(ADAPTER *a,
			   void *addr,
			   const dword *data,
			   int dwords);
	void (*ram_in_dw)(ADAPTER *a,
			  void *addr,
			  dword *data,
			  int dwords);
	void (*istream_wakeup)(ADAPTER *a);
#else
	byte stream_buffer[4];
#endif
};
/*------------------------------------------------------------------*/
/* public functions of IDI common code                              */
/*------------------------------------------------------------------*/
void pr_out(ADAPTER *a);
byte pr_dpc(ADAPTER *a);
byte scom_test_int(ADAPTER *a);
void scom_clear_int(ADAPTER *a);
/*------------------------------------------------------------------*/
/* OS specific functions used by IDI common code                    */
/*------------------------------------------------------------------*/
void free_entity(ADAPTER *a, byte e_no);
void assign_queue(ADAPTER *a, byte e_no, word ref);
byte get_assign(ADAPTER *a, word ref);
void req_queue(ADAPTER *a, byte e_no);
byte look_req(ADAPTER *a);
void next_req(ADAPTER *a);
ENTITY *entity_ptr(ADAPTER *a, byte e_no);
#if defined(DIVA_ISTREAM)
struct _diva_xdi_stream_interface;
void diva_xdi_provide_istream_info(ADAPTER *a,
				   struct _diva_xdi_stream_interface *pI);
void pr_stream(ADAPTER *a);
int diva_istream_write(void *context,
		       int Id,
		       void *data,
		       int length,
		       int final,
		       byte usr1,
		       byte usr2);
int diva_istream_read(void *context,
		      int Id,
		      void *data,
		      int max_length,
		      int *final,
		      byte *usr1,
		      byte *usr2);
#if defined(DIVA_IDI_RX_DMA)
#include "diva_dma.h"
#endif
#endif

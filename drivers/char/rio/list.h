/****************************************************************************
 *******                                                              *******
 *******                      L I S T                                 *******
 *******                                                              *******
 ****************************************************************************

 Author  : Jeremy Rolls.
 Date    : 04-Nov-1990

 *
 *  (C) 1990 - 2000 Specialix International Ltd., Byfleet, Surrey, UK.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 Version : 0.01


                            Mods
 ----------------------------------------------------------------------------
  Date     By                Description
 ----------------------------------------------------------------------------
 ***************************************************************************/

#ifndef _list_h
#define _list_h 1

#ifdef SCCS_LABELS
#ifndef lint
static char *_rio_list_h_sccs = "@(#)list.h	1.9";
#endif
#endif

#define PKT_IN_USE    0x1

#ifdef INKERNEL

#define ZERO_PTR (ushort) 0x8000
#define	CaD	PortP->Caddr

/*
** We can add another packet to a transmit queue if the packet pointer pointed
** to by the TxAdd pointer has PKT_IN_USE clear in its address.
*/

#ifndef linux
#if defined( MIPS ) && !defined( MIPSEISA )
/* May the shoes of the Devil dance on your grave for creating this */
#define   can_add_transmit(PacketP,PortP) \
          (!((uint)(PacketP = (struct PKT *)RIO_PTR(CaD,RINDW(PortP->TxAdd))) \
          & (PKT_IN_USE<<2)))

#elif  defined(MIPSEISA) || defined(nx6000) || \
       defined(drs6000)  || defined(UWsparc)

#define   can_add_transmit(PacketP,PortP) \
          (!((uint)(PacketP = (struct PKT *)RIO_PTR(CaD,RINDW(PortP->TxAdd))) \
	  & PKT_IN_USE))

#else
#define   can_add_transmit(PacketP,PortP) \
          (!((uint)(PacketP = (struct PKT *)RIO_PTR(CaD,*PortP->TxAdd)) \
	  & PKT_IN_USE))
#endif

/*
** To add a packet to the queue, you set the PKT_IN_USE bit in the address,
** and then move the TxAdd pointer along one position to point to the next
** packet pointer. You must wrap the pointer from the end back to the start.
*/
#if defined(MIPS) || defined(nx6000) || defined(drs6000) || defined(UWsparc)
#   define add_transmit(PortP)  \
	WINDW(PortP->TxAdd,RINDW(PortP->TxAdd) | PKT_IN_USE);\
	if (PortP->TxAdd == PortP->TxEnd)\
	    PortP->TxAdd = PortP->TxStart;\
	else\
	    PortP->TxAdd++;\
	WWORD(PortP->PhbP->tx_add , RIO_OFF(CaD,PortP->TxAdd));
#elif defined(AIX)
#   define add_transmit(PortP)  \
	{\
	    register ushort *TxAddP = (ushort *)RIO_PTR(Cad,PortP->TxAddO);\
	    WINDW( TxAddP, RINDW( TxAddP ) | PKT_IN_USE );\
	    if (PortP->TxAddO == PortP->TxEndO )\
		PortP->TxAddO = PortP->TxStartO;\
	    else\
		PortP->TxAddO += sizeof(ushort);\
	    WWORD(((PHB *)RIO_PTR(Cad,PortP->PhbO))->tx_add , PortP->TxAddO );\
	}
#else
#   define add_transmit(PortP)  \
	*PortP->TxAdd |= PKT_IN_USE;\
	if (PortP->TxAdd == PortP->TxEnd)\
	    PortP->TxAdd = PortP->TxStart;\
	else\
	    PortP->TxAdd++;\
	PortP->PhbP->tx_add = RIO_OFF(CaD,PortP->TxAdd);
#endif

/*
** can_remove_receive( PacketP, PortP ) returns non-zero if PKT_IN_USE is set
** for the next packet on the queue. It will also set PacketP to point to the
** relevant packet, [having cleared the PKT_IN_USE bit]. If PKT_IN_USE is clear,
** then can_remove_receive() returns 0.
*/
#if defined(MIPS) || defined(nx6000) || defined(drs6000) || defined(UWsparc)
#   define can_remove_receive(PacketP,PortP) \
	((RINDW(PortP->RxRemove) & PKT_IN_USE) ? \
	(PacketP=(struct PKT *)RIO_PTR(CaD,(RINDW(PortP->RxRemove) & ~PKT_IN_USE))):0)
#elif defined(AIX)
#   define can_remove_receive(PacketP,PortP) \
	((RINDW((ushort *)RIO_PTR(Cad,PortP->RxRemoveO)) & PKT_IN_USE) ? \
	(PacketP=(struct PKT *)RIO_PTR(Cad,RINDW((ushort *)RIO_PTR(Cad,PortP->RxRemoveO)) & ~PKT_IN_USE)):0)
#else
#   define can_remove_receive(PacketP,PortP) \
	((*PortP->RxRemove & PKT_IN_USE) ? \
	(PacketP=(struct PKT *)RIO_PTR(CaD,(*PortP->RxRemove & ~PKT_IN_USE))):0)
#endif


/*
** Will God see it within his heart to forgive us for this thing that
** we have created? To remove a packet from the receive queue you clear
** its PKT_IN_USE bit, and then bump the pointers. Once the pointers
** get to the end, they must be wrapped back to the start.
*/
#if defined(MIPS) || defined(nx6000) || defined(drs6000) || defined(UWsparc)
#   define remove_receive(PortP) \
	WINDW(PortP->RxRemove, (RINDW(PortP->RxRemove) & ~PKT_IN_USE));\
	if (PortP->RxRemove == PortP->RxEnd)\
	    PortP->RxRemove = PortP->RxStart;\
	else\
	    PortP->RxRemove++;\
	WWORD(PortP->PhbP->rx_remove , RIO_OFF(CaD,PortP->RxRemove));
#elif defined(AIX)
#   define remove_receive(PortP) \
    {\
        register ushort *RxRemoveP = (ushort *)RIO_PTR(Cad,PortP->RxRemoveO);\
        WINDW( RxRemoveP, RINDW( RxRemoveP ) & ~PKT_IN_USE );\
        if (PortP->RxRemoveO == PortP->RxEndO)\
            PortP->RxRemoveO = PortP->RxStartO;\
        else\
            PortP->RxRemoveO += sizeof(ushort);\
        WWORD(((PHB *)RIO_PTR(Cad,PortP->PhbO))->rx_remove, PortP->RxRemoveO );\
    }
#else
#   define remove_receive(PortP) \
	*PortP->RxRemove &= ~PKT_IN_USE;\
	if (PortP->RxRemove == PortP->RxEnd)\
	    PortP->RxRemove = PortP->RxStart;\
	else\
	    PortP->RxRemove++;\
	PortP->PhbP->rx_remove = RIO_OFF(CaD,PortP->RxRemove);
#endif
#endif


#else				/* !IN_KERNEL */

#define ZERO_PTR NULL


#ifdef HOST
/* #define can_remove_transmit(pkt,phb) ((((char*)pkt = (*(char**)(phb->tx_remove))-1) || 1)) && (*phb->u3.s2.tx_remove_ptr & PKT_IN_USE))   */
#define remove_transmit(phb) *phb->u3.s2.tx_remove_ptr &= ~(ushort)PKT_IN_USE;\
                             if (phb->tx_remove == phb->tx_end)\
                                phb->tx_remove = phb->tx_start;\
                             else\
                                phb->tx_remove++;
#define can_add_receive(phb) !(*phb->u4.s2.rx_add_ptr & PKT_IN_USE)
#define add_receive(pkt,phb) *phb->rx_add = pkt;\
                             *phb->u4.s2.rx_add_ptr |= PKT_IN_USE;\
                             if (phb->rx_add == phb->rx_end)\
                                phb->rx_add = phb->rx_start;\
                             else\
                                phb->rx_add++;
#endif
#endif

#ifdef RTA
#define splx(oldspl)    if ((oldspl) == 0) spl0()
#endif

#endif				/* ifndef _list.h */
/*********** end of file ***********/

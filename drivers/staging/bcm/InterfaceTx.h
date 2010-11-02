#ifndef _INTERFACE_TX_H
#define _INTERFACE_TX_H

INT InterfaceTransmitPacket(PVOID arg, PVOID data, UINT len);


ULONG InterfaceTxDataPacket(PMINI_ADAPTER Adapter,PVOID Packet,USHORT usVcid);

ULONG InterfaceTxControlPacket(PMINI_ADAPTER Adapter,PVOID pvBuffer,UINT uiBufferLength);


#endif


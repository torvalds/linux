/* 2001/10/02
 *
 * enternow.h   Header-file included by
 *              enternow_pci.c
 *
 * Author       Christoph Ersfeld <info@formula-n.de>
 *              Formula-n Europe AG (www.formula-n.com)
 *              previously Gerdes AG
 *
 *
 *              This file is (c) under GNU PUBLIC LICENSE
 */


/* ***************************************************************************************** *
 * ****************************** datatypes and macros ************************************* *
 * ***************************************************************************************** */

#define BYTE							unsigned char
#define WORD							unsigned int
#define HIBYTE(w)						((unsigned char)((w & 0xff00) / 256))
#define LOBYTE(w)						((unsigned char)(w & 0x00ff))
#define InByte(addr)						inb(addr)
#define OutByte(addr,val)					outb(val,addr)



/* ***************************************************************************************** *
 * *********************************** card-specific *************************************** *
 * ***************************************************************************************** */

/* für PowerISDN PCI */
#define TJ_AMD_IRQ 						0x20
#define TJ_LED1 						0x40
#define TJ_LED2 						0x80


/* Das Fenster zum AMD...
 * Ab Adresse hw.njet.base + TJ_AMD_PORT werden vom AMD jeweils 8 Bit in
 * den TigerJet i/o-Raum gemappt
 * -> 0x01 des AMD bei hw.njet.base + 0C4 */
#define TJ_AMD_PORT						0xC0



/* ***************************************************************************************** *
 * *************************************** Prototypen ************************************** *
 * ***************************************************************************************** */

BYTE ReadByteAmd7930(struct IsdnCardState *cs, BYTE offset);
void WriteByteAmd7930(struct IsdnCardState *cs, BYTE offset, BYTE value);

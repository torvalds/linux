/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 * Registers and bits for amccs5933 pci chip
 *    copyright            : (C) 2002 by Frank Mori Hess
 ***************************************************************************/

// register offsets
enum {
	MBEF_REG = 0x34,	// mailbux empty/full
	INTCSR_REG = 0x38,	// interrupt control and status
	BMCSR_REG = 0x3c,	// bus master control and status
};

// incoming mailbox 0-3  register offsets
extern inline int INCOMING_MAILBOX_REG(unsigned int mailbox)
{
	return (0x10 + 4 * mailbox);
};

// bit definitions

// INTCSR bits
enum {
	OUTBOX_EMPTY_INTR_BIT = 0x10,	// enable outbox empty interrupt
	INBOX_FULL_INTR_BIT = 0x1000,	// enable inbox full interrupt
	INBOX_INTR_CS_BIT = 0x20000, // read, or write clear inbox full interrupt
	INTR_ASSERTED_BIT = 0x800000,	// read only, interrupt asserted
};

// select byte 0 to 3 of incoming mailbox
extern inline int INBOX_BYTE_BITS(unsigned int byte)
{
	return (byte & 0x3) << 8;
};

// select incoming mailbox 0 to 3
extern inline int INBOX_SELECT_BITS(unsigned int mailbox)
{
	return (mailbox & 0x3) << 10;
};

// select byte 0 to 3 of outgoing mailbox
extern inline int OUTBOX_BYTE_BITS(unsigned int byte)
{
	return (byte & 0x3);
};

// select outgoing mailbox 0 to 3
extern inline int OUTBOX_SELECT_BITS(unsigned int mailbox)
{
	return (mailbox & 0x3) << 2;
};

//BMCSR bits
enum {
	MBOX_FLAGS_RESET_BIT = 0x08000000,	// resets mailbox empty/full flags
};


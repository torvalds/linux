/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * iohelper.h
 *		helper for define functions to access ISDN hardware
 *              supported are memory mapped IO
 *		indirect port IO (one port for address, one for data)
 *
 * Author       Karsten Keil <keil@isdn4linux.de>
 *
 * Copyright 2009  by Karsten Keil <keil@isdn4linux.de>
 */

#ifndef _IOHELPER_H
#define _IOHELPER_H

typedef	u8	(read_reg_func)(void *hwp, u8 offset);
			       typedef	void	(write_reg_func)(void *hwp, u8 offset, u8 value);
			       typedef	void	(fifo_func)(void *hwp, u8 offset, u8 *datap, int size);

			       struct _ioport {
				       u32	port;
				       u32	ale;
			       };

#define IOFUNC_IO(name, hws, ap)					\
	static u8 Read##name##_IO(void *p, u8 off) {			\
		struct hws *hw = p;					\
		return inb(hw->ap.port + off);				\
	}								\
	static void Write##name##_IO(void *p, u8 off, u8 val) {		\
		struct hws *hw = p;					\
		outb(val, hw->ap.port + off);				\
	}								\
	static void ReadFiFo##name##_IO(void *p, u8 off, u8 *dp, int size) { \
		struct hws *hw = p;					\
		insb(hw->ap.port + off, dp, size);			\
	}								\
	static void WriteFiFo##name##_IO(void *p, u8 off, u8 *dp, int size) { \
		struct hws *hw = p;					\
		outsb(hw->ap.port + off, dp, size);			\
	}

#define IOFUNC_IND(name, hws, ap)					\
	static u8 Read##name##_IND(void *p, u8 off) {			\
		struct hws *hw = p;					\
		outb(off, hw->ap.ale);					\
		return inb(hw->ap.port);				\
	}								\
	static void Write##name##_IND(void *p, u8 off, u8 val) {	\
		struct hws *hw = p;					\
		outb(off, hw->ap.ale);					\
		outb(val, hw->ap.port);					\
	}								\
	static void ReadFiFo##name##_IND(void *p, u8 off, u8 *dp, int size) { \
		struct hws *hw = p;					\
		outb(off, hw->ap.ale);					\
		insb(hw->ap.port, dp, size);				\
	}								\
	static void WriteFiFo##name##_IND(void *p, u8 off, u8 *dp, int size) { \
		struct hws *hw = p;					\
		outb(off, hw->ap.ale);					\
		outsb(hw->ap.port, dp, size);				\
	}

#define IOFUNC_MEMIO(name, hws, typ, adr)				\
	static u8 Read##name##_MIO(void *p, u8 off) {			\
		struct hws *hw = p;					\
		return readb(((typ *)hw->adr) + off);			\
	}								\
	static void Write##name##_MIO(void *p, u8 off, u8 val) {	\
		struct hws *hw = p;					\
		writeb(val, ((typ *)hw->adr) + off);			\
	}								\
	static void ReadFiFo##name##_MIO(void *p, u8 off, u8 *dp, int size) { \
		struct hws *hw = p;					\
		while (size--)						\
			*dp++ = readb(((typ *)hw->adr) + off);		\
	}								\
	static void WriteFiFo##name##_MIO(void *p, u8 off, u8 *dp, int size) { \
		struct hws *hw = p;					\
		while (size--)						\
			writeb(*dp++, ((typ *)hw->adr) + off);		\
	}

#define ASSIGN_FUNC(typ, name, dest)	do {			\
		dest.read_reg = &Read##name##_##typ;		\
		dest.write_reg = &Write##name##_##typ;		\
		dest.read_fifo = &ReadFiFo##name##_##typ;	\
		dest.write_fifo = &WriteFiFo##name##_##typ;	\
	} while (0)
#define ASSIGN_FUNC_IPAC(typ, target)	do {		\
		ASSIGN_FUNC(typ, ISAC, target.isac);	\
		ASSIGN_FUNC(typ, IPAC, target);		\
	} while (0)

#endif

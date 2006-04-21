/* -*- mode: c; c-basic-offset: 8 -*- */

/* Copyright (C) 1999,2001
 *
 * Author: J.E.J.Bottomley@HansenPartnership.com
 *
 * linux/arch/i386/kernel/voyager_cat.c
 *
 * This file contains all the logic for manipulating the CAT bus
 * in a level 5 machine.
 *
 * The CAT bus is a serial configuration and test bus.  Its primary
 * uses are to probe the initial configuration of the system and to
 * diagnose error conditions when a system interrupt occurs.  The low
 * level interface is fairly primitive, so most of this file consists
 * of bit shift manipulations to send and receive packets on the
 * serial bus */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/completion.h>
#include <linux/sched.h>
#include <asm/voyager.h>
#include <asm/vic.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/io.h>

#ifdef VOYAGER_CAT_DEBUG
#define CDEBUG(x)	printk x
#else
#define CDEBUG(x)
#endif

/* the CAT command port */
#define CAT_CMD		(sspb + 0xe)
/* the CAT data port */
#define CAT_DATA	(sspb + 0xd)

/* the internal cat functions */
static void cat_pack(__u8 *msg, __u16 start_bit, __u8 *data, 
		     __u16 num_bits);
static void cat_unpack(__u8 *msg, __u16 start_bit, __u8 *data,
		       __u16 num_bits);
static void cat_build_header(__u8 *header, const __u16 len, 
			     const __u16 smallest_reg_bits,
			     const __u16 longest_reg_bits);
static int cat_sendinst(voyager_module_t *modp, voyager_asic_t *asicp,
			__u8 reg, __u8 op);
static int cat_getdata(voyager_module_t *modp, voyager_asic_t *asicp,
		       __u8 reg, __u8 *value);
static int cat_shiftout(__u8 *data, __u16 data_bytes, __u16 header_bytes,
			__u8 pad_bits);
static int cat_write(voyager_module_t *modp, voyager_asic_t *asicp, __u8 reg,
		     __u8 value);
static int cat_read(voyager_module_t *modp, voyager_asic_t *asicp, __u8 reg,
		    __u8 *value);
static int cat_subread(voyager_module_t *modp, voyager_asic_t *asicp,
		       __u16 offset, __u16 len, void *buf);
static int cat_senddata(voyager_module_t *modp, voyager_asic_t *asicp,
			__u8 reg, __u8 value);
static int cat_disconnect(voyager_module_t *modp, voyager_asic_t *asicp);
static int cat_connect(voyager_module_t *modp, voyager_asic_t *asicp);

static inline const char *
cat_module_name(int module_id)
{
	switch(module_id) {
	case 0x10:
		return "Processor Slot 0";
	case 0x11:
		return "Processor Slot 1";
	case 0x12:
		return "Processor Slot 2";
	case 0x13:
		return "Processor Slot 4";
	case 0x14:
		return "Memory Slot 0";
	case 0x15:
		return "Memory Slot 1";
	case 0x18:
		return "Primary Microchannel";
	case 0x19:
		return "Secondary Microchannel";
	case 0x1a:
		return "Power Supply Interface";
	case 0x1c:
		return "Processor Slot 5";
	case 0x1d:
		return "Processor Slot 6";
	case 0x1e:
		return "Processor Slot 7";
	case 0x1f:
		return "Processor Slot 8";
	default:
		return "Unknown Module";
	}
}

static int sspb = 0;		/* stores the super port location */
int voyager_8slot = 0;		/* set to true if a 51xx monster */

voyager_module_t *voyager_cat_list;

/* the I/O port assignments for the VIC and QIC */
static struct resource vic_res = {
	.name	= "Voyager Interrupt Controller",
	.start	= 0xFC00,
	.end	= 0xFC6F
};
static struct resource qic_res = {
	.name	= "Quad Interrupt Controller",
	.start	= 0xFC70,
	.end	= 0xFCFF
};

/* This function is used to pack a data bit stream inside a message.
 * It writes num_bits of the data buffer in msg starting at start_bit.
 * Note: This function assumes that any unused bit in the data stream
 * is set to zero so that the ors will work correctly */
static void
cat_pack(__u8 *msg, const __u16 start_bit, __u8 *data, const __u16 num_bits)
{
	/* compute initial shift needed */
	const __u16 offset = start_bit % BITS_PER_BYTE;
	__u16 len = num_bits / BITS_PER_BYTE;
	__u16 byte = start_bit / BITS_PER_BYTE;
	__u16 residue = (num_bits % BITS_PER_BYTE) + offset;
	int i;

	/* adjust if we have more than a byte of residue */
	if(residue >= BITS_PER_BYTE) {
		residue -= BITS_PER_BYTE;
		len++;
	}

	/* clear out the bits.  We assume here that if len==0 then
	 * residue >= offset.  This is always true for the catbus
	 * operations */
	msg[byte] &= 0xff << (BITS_PER_BYTE - offset); 
	msg[byte++] |= data[0] >> offset;
	if(len == 0)
		return;
	for(i = 1; i < len; i++)
		msg[byte++] = (data[i-1] << (BITS_PER_BYTE - offset))
			| (data[i] >> offset);
	if(residue != 0) {
		__u8 mask = 0xff >> residue;
		__u8 last_byte = data[i-1] << (BITS_PER_BYTE - offset)
			| (data[i] >> offset);
		
		last_byte &= ~mask;
		msg[byte] &= mask;
		msg[byte] |= last_byte;
	}
	return;
}
/* unpack the data again (same arguments as cat_pack()). data buffer
 * must be zero populated.
 *
 * Function: given a message string move to start_bit and copy num_bits into
 * data (starting at bit 0 in data).
 */
static void
cat_unpack(__u8 *msg, const __u16 start_bit, __u8 *data, const __u16 num_bits)
{
	/* compute initial shift needed */
	const __u16 offset = start_bit % BITS_PER_BYTE;
	__u16 len = num_bits / BITS_PER_BYTE;
	const __u8 last_bits = num_bits % BITS_PER_BYTE;
	__u16 byte = start_bit / BITS_PER_BYTE;
	int i;

	if(last_bits != 0)
		len++;

	/* special case: want < 8 bits from msg and we can get it from
	 * a single byte of the msg */
	if(len == 0 && BITS_PER_BYTE - offset >= num_bits) {
		data[0] = msg[byte] << offset;
		data[0] &= 0xff >> (BITS_PER_BYTE - num_bits);
		return;
	}
	for(i = 0; i < len; i++) {
		/* this annoying if has to be done just in case a read of
		 * msg one beyond the array causes a panic */
		if(offset != 0) {
			data[i] = msg[byte++] << offset;
			data[i] |= msg[byte] >> (BITS_PER_BYTE - offset);
		}
		else {
			data[i] = msg[byte++];
		}
	}
	/* do we need to truncate the final byte */
	if(last_bits != 0) {
		data[i-1] &= 0xff << (BITS_PER_BYTE - last_bits);
	}
	return;
}

static void
cat_build_header(__u8 *header, const __u16 len, const __u16 smallest_reg_bits,
		 const __u16 longest_reg_bits)
{
	int i;
	__u16 start_bit = (smallest_reg_bits - 1) % BITS_PER_BYTE;
	__u8 *last_byte = &header[len - 1];

	if(start_bit == 0)
		start_bit = 1;	/* must have at least one bit in the hdr */
	
	for(i=0; i < len; i++)
		header[i] = 0;

	for(i = start_bit; i > 0; i--)
		*last_byte = ((*last_byte) << 1) + 1;

}

static int
cat_sendinst(voyager_module_t *modp, voyager_asic_t *asicp, __u8 reg, __u8 op)
{
	__u8 parity, inst, inst_buf[4] = { 0 };
	__u8 iseq[VOYAGER_MAX_SCAN_PATH], hseq[VOYAGER_MAX_REG_SIZE];
	__u16 ibytes, hbytes, padbits;
	int i;
	
	/* 
	 * Parity is the parity of the register number + 1 (READ_REGISTER
	 * and WRITE_REGISTER always add '1' to the number of bits == 1)
	 */
	parity = (__u8)(1 + (reg & 0x01) +
	         ((__u8)(reg & 0x02) >> 1) +
	         ((__u8)(reg & 0x04) >> 2) +
	         ((__u8)(reg & 0x08) >> 3)) % 2;

	inst = ((parity << 7) | (reg << 2) | op);

	outb(VOYAGER_CAT_IRCYC, CAT_CMD);
	if(!modp->scan_path_connected) {
		if(asicp->asic_id != VOYAGER_CAT_ID) {
			printk("**WARNING***: cat_sendinst has disconnected scan path not to CAT asic\n");
			return 1;
		}
		outb(VOYAGER_CAT_HEADER, CAT_DATA);
		outb(inst, CAT_DATA);
		if(inb(CAT_DATA) != VOYAGER_CAT_HEADER) {
			CDEBUG(("VOYAGER CAT: cat_sendinst failed to get CAT_HEADER\n"));
			return 1;
		}
		return 0;
	}
	ibytes = modp->inst_bits / BITS_PER_BYTE;
	if((padbits = modp->inst_bits % BITS_PER_BYTE) != 0) {
		padbits = BITS_PER_BYTE - padbits;
		ibytes++;
	}
	hbytes = modp->largest_reg / BITS_PER_BYTE;
	if(modp->largest_reg % BITS_PER_BYTE)
		hbytes++;
	CDEBUG(("cat_sendinst: ibytes=%d, hbytes=%d\n", ibytes, hbytes));
	/* initialise the instruction sequence to 0xff */
	for(i=0; i < ibytes + hbytes; i++)
		iseq[i] = 0xff;
	cat_build_header(hseq, hbytes, modp->smallest_reg, modp->largest_reg);
	cat_pack(iseq, modp->inst_bits, hseq, hbytes * BITS_PER_BYTE);
	inst_buf[0] = inst;
	inst_buf[1] = 0xFF >> (modp->largest_reg % BITS_PER_BYTE);
	cat_pack(iseq, asicp->bit_location, inst_buf, asicp->ireg_length);
#ifdef VOYAGER_CAT_DEBUG
	printk("ins = 0x%x, iseq: ", inst);
	for(i=0; i< ibytes + hbytes; i++)
		printk("0x%x ", iseq[i]);
	printk("\n");
#endif
	if(cat_shiftout(iseq, ibytes, hbytes, padbits)) {
		CDEBUG(("VOYAGER CAT: cat_sendinst: cat_shiftout failed\n"));
		return 1;
	}
	CDEBUG(("CAT SHIFTOUT DONE\n"));
	return 0;
}

static int
cat_getdata(voyager_module_t *modp, voyager_asic_t *asicp, __u8 reg, 
	    __u8 *value)
{
	if(!modp->scan_path_connected) {
		if(asicp->asic_id != VOYAGER_CAT_ID) {
			CDEBUG(("VOYAGER CAT: ERROR: cat_getdata to CAT asic with scan path connected\n"));
			return 1;
		}
		if(reg > VOYAGER_SUBADDRHI) 
			outb(VOYAGER_CAT_RUN, CAT_CMD);
		outb(VOYAGER_CAT_DRCYC, CAT_CMD);
		outb(VOYAGER_CAT_HEADER, CAT_DATA);
		*value = inb(CAT_DATA);
		outb(0xAA, CAT_DATA);
		if(inb(CAT_DATA) != VOYAGER_CAT_HEADER) {
			CDEBUG(("cat_getdata: failed to get VOYAGER_CAT_HEADER\n"));
			return 1;
		}
		return 0;
	}
	else {
		__u16 sbits = modp->num_asics -1 + asicp->ireg_length;
		__u16 sbytes = sbits / BITS_PER_BYTE;
		__u16 tbytes;
		__u8 string[VOYAGER_MAX_SCAN_PATH], trailer[VOYAGER_MAX_REG_SIZE];
		__u8 padbits;
		int i;
		
		outb(VOYAGER_CAT_DRCYC, CAT_CMD);

		if((padbits = sbits % BITS_PER_BYTE) != 0) {
			padbits = BITS_PER_BYTE - padbits;
			sbytes++;
		}
		tbytes = asicp->ireg_length / BITS_PER_BYTE;
		if(asicp->ireg_length % BITS_PER_BYTE)
			tbytes++;
		CDEBUG(("cat_getdata: tbytes = %d, sbytes = %d, padbits = %d\n",
			tbytes,	sbytes, padbits));
		cat_build_header(trailer, tbytes, 1, asicp->ireg_length);

		
		for(i = tbytes - 1; i >= 0; i--) {
			outb(trailer[i], CAT_DATA);
			string[sbytes + i] = inb(CAT_DATA);
		}

		for(i = sbytes - 1; i >= 0; i--) {
			outb(0xaa, CAT_DATA);
			string[i] = inb(CAT_DATA);
		}
		*value = 0;
		cat_unpack(string, padbits + (tbytes * BITS_PER_BYTE) + asicp->asic_location, value, asicp->ireg_length);
#ifdef VOYAGER_CAT_DEBUG
		printk("value=0x%x, string: ", *value);
		for(i=0; i< tbytes+sbytes; i++)
			printk("0x%x ", string[i]);
		printk("\n");
#endif
		
		/* sanity check the rest of the return */
		for(i=0; i < tbytes; i++) {
			__u8 input = 0;

			cat_unpack(string, padbits + (i * BITS_PER_BYTE), &input, BITS_PER_BYTE);
			if(trailer[i] != input) {
				CDEBUG(("cat_getdata: failed to sanity check rest of ret(%d) 0x%x != 0x%x\n", i, input, trailer[i]));
				return 1;
			}
		}
		CDEBUG(("cat_getdata DONE\n"));
		return 0;
	}
}

static int
cat_shiftout(__u8 *data, __u16 data_bytes, __u16 header_bytes, __u8 pad_bits)
{
	int i;
	
	for(i = data_bytes + header_bytes - 1; i >= header_bytes; i--)
		outb(data[i], CAT_DATA);

	for(i = header_bytes - 1; i >= 0; i--) {
		__u8 header = 0;
		__u8 input;

		outb(data[i], CAT_DATA);
		input = inb(CAT_DATA);
		CDEBUG(("cat_shiftout: returned 0x%x\n", input));
		cat_unpack(data, ((data_bytes + i) * BITS_PER_BYTE) - pad_bits,
			   &header, BITS_PER_BYTE);
		if(input != header) {
			CDEBUG(("VOYAGER CAT: cat_shiftout failed to return header 0x%x != 0x%x\n", input, header));
			return 1;
		}
	}
	return 0;
}

static int
cat_senddata(voyager_module_t *modp, voyager_asic_t *asicp, 
	     __u8 reg, __u8 value)
{
	outb(VOYAGER_CAT_DRCYC, CAT_CMD);
	if(!modp->scan_path_connected) {
		if(asicp->asic_id != VOYAGER_CAT_ID) {
			CDEBUG(("VOYAGER CAT: ERROR: scan path disconnected when asic != CAT\n"));
			return 1;
		}
		outb(VOYAGER_CAT_HEADER, CAT_DATA);
		outb(value, CAT_DATA);
		if(inb(CAT_DATA) != VOYAGER_CAT_HEADER) {
			CDEBUG(("cat_senddata: failed to get correct header response to sent data\n"));
			return 1;
		}
		if(reg > VOYAGER_SUBADDRHI) {
			outb(VOYAGER_CAT_RUN, CAT_CMD);
			outb(VOYAGER_CAT_END, CAT_CMD);
			outb(VOYAGER_CAT_RUN, CAT_CMD);
		}
		
		return 0;
	}
	else {
		__u16 hbytes = asicp->ireg_length / BITS_PER_BYTE;
		__u16 dbytes = (modp->num_asics - 1 + asicp->ireg_length)/BITS_PER_BYTE;
		__u8 padbits, dseq[VOYAGER_MAX_SCAN_PATH], 
			hseq[VOYAGER_MAX_REG_SIZE];
		int i;

		if((padbits = (modp->num_asics - 1 
			       + asicp->ireg_length) % BITS_PER_BYTE) != 0) {
			padbits = BITS_PER_BYTE - padbits;
			dbytes++;
		}
		if(asicp->ireg_length % BITS_PER_BYTE)
			hbytes++;
		
		cat_build_header(hseq, hbytes, 1, asicp->ireg_length);
		
		for(i = 0; i < dbytes + hbytes; i++)
			dseq[i] = 0xff;
		CDEBUG(("cat_senddata: dbytes=%d, hbytes=%d, padbits=%d\n",
			dbytes, hbytes, padbits));
		cat_pack(dseq, modp->num_asics - 1 + asicp->ireg_length,
			 hseq, hbytes * BITS_PER_BYTE);
		cat_pack(dseq, asicp->asic_location, &value, 
			 asicp->ireg_length);
#ifdef VOYAGER_CAT_DEBUG
		printk("dseq ");
		for(i=0; i<hbytes+dbytes; i++) {
			printk("0x%x ", dseq[i]);
		}
		printk("\n");
#endif
		return cat_shiftout(dseq, dbytes, hbytes, padbits);
	}
}

static int
cat_write(voyager_module_t *modp, voyager_asic_t *asicp, __u8 reg,
	 __u8 value)
{
	if(cat_sendinst(modp, asicp, reg, VOYAGER_WRITE_CONFIG))
		return 1;
	return cat_senddata(modp, asicp, reg, value);
}

static int
cat_read(voyager_module_t *modp, voyager_asic_t *asicp, __u8 reg,
	 __u8 *value)
{
	if(cat_sendinst(modp, asicp, reg, VOYAGER_READ_CONFIG))
		return 1;
	return cat_getdata(modp, asicp, reg, value);
}

static int
cat_subaddrsetup(voyager_module_t *modp, voyager_asic_t *asicp, __u16 offset,
		 __u16 len)
{
	__u8 val;

	if(len > 1) {
		/* set auto increment */
		__u8 newval;
		
		if(cat_read(modp, asicp, VOYAGER_AUTO_INC_REG, &val)) {
			CDEBUG(("cat_subaddrsetup: read of VOYAGER_AUTO_INC_REG failed\n"));
			return 1;
		}
		CDEBUG(("cat_subaddrsetup: VOYAGER_AUTO_INC_REG = 0x%x\n", val));
		newval = val | VOYAGER_AUTO_INC;
		if(newval != val) {
			if(cat_write(modp, asicp, VOYAGER_AUTO_INC_REG, val)) {
				CDEBUG(("cat_subaddrsetup: write to VOYAGER_AUTO_INC_REG failed\n"));
				return 1;
			}
		}
	}
	if(cat_write(modp, asicp, VOYAGER_SUBADDRLO, (__u8)(offset &0xff))) {
		CDEBUG(("cat_subaddrsetup: write to SUBADDRLO failed\n"));
		return 1;
	}
	if(asicp->subaddr > VOYAGER_SUBADDR_LO) {
		if(cat_write(modp, asicp, VOYAGER_SUBADDRHI, (__u8)(offset >> 8))) {
			CDEBUG(("cat_subaddrsetup: write to SUBADDRHI failed\n"));
			return 1;
		}
		cat_read(modp, asicp, VOYAGER_SUBADDRHI, &val);
		CDEBUG(("cat_subaddrsetup: offset = %d, hi = %d\n", offset, val));
	}
	cat_read(modp, asicp, VOYAGER_SUBADDRLO, &val);
	CDEBUG(("cat_subaddrsetup: offset = %d, lo = %d\n", offset, val));
	return 0;
}
		
static int
cat_subwrite(voyager_module_t *modp, voyager_asic_t *asicp, __u16 offset,
	    __u16 len, void *buf)
{
	int i, retval;

	/* FIXME: need special actions for VOYAGER_CAT_ID here */
	if(asicp->asic_id == VOYAGER_CAT_ID) {
		CDEBUG(("cat_subwrite: ATTEMPT TO WRITE TO CAT ASIC\n"));
		/* FIXME -- This is supposed to be handled better
		 * There is a problem writing to the cat asic in the
		 * PSI.  The 30us delay seems to work, though */
		udelay(30);
	}
		
	if((retval = cat_subaddrsetup(modp, asicp, offset, len)) != 0) {
		printk("cat_subwrite: cat_subaddrsetup FAILED\n");
		return retval;
	}
	
	if(cat_sendinst(modp, asicp, VOYAGER_SUBADDRDATA, VOYAGER_WRITE_CONFIG)) {
		printk("cat_subwrite: cat_sendinst FAILED\n");
		return 1;
	}
	for(i = 0; i < len; i++) {
		if(cat_senddata(modp, asicp, 0xFF, ((__u8 *)buf)[i])) {
			printk("cat_subwrite: cat_sendata element at %d FAILED\n", i);
			return 1;
		}
	}
	return 0;
}
static int
cat_subread(voyager_module_t *modp, voyager_asic_t *asicp, __u16 offset,
	    __u16 len, void *buf)
{
	int i, retval;

	if((retval = cat_subaddrsetup(modp, asicp, offset, len)) != 0) {
		CDEBUG(("cat_subread: cat_subaddrsetup FAILED\n"));
		return retval;
	}

	if(cat_sendinst(modp, asicp, VOYAGER_SUBADDRDATA, VOYAGER_READ_CONFIG)) {
		CDEBUG(("cat_subread: cat_sendinst failed\n"));
		return 1;
	}
	for(i = 0; i < len; i++) {
		if(cat_getdata(modp, asicp, 0xFF,
			       &((__u8 *)buf)[i])) {
			CDEBUG(("cat_subread: cat_getdata element %d failed\n", i));
			return 1;
		}
	}
	return 0;
}


/* buffer for storing EPROM data read in during initialisation */
static __initdata __u8 eprom_buf[0xFFFF];
static voyager_module_t *voyager_initial_module;

/* Initialise the cat bus components.  We assume this is called by the
 * boot cpu *after* all memory initialisation has been done (so we can
 * use kmalloc) but before smp initialisation, so we can probe the SMP
 * configuration and pick up necessary information.  */
void
voyager_cat_init(void)
{
	voyager_module_t **modpp = &voyager_initial_module;
	voyager_asic_t **asicpp;
	voyager_asic_t *qabc_asic = NULL;
	int i, j;
	unsigned long qic_addr = 0;
	__u8 qabc_data[0x20];
	__u8 num_submodules, val;
	voyager_eprom_hdr_t *eprom_hdr = (voyager_eprom_hdr_t *)&eprom_buf[0];
	
	__u8 cmos[4];
	unsigned long addr;
	
	/* initiallise the SUS mailbox */
	for(i=0; i<sizeof(cmos); i++)
		cmos[i] = voyager_extended_cmos_read(VOYAGER_DUMP_LOCATION + i);
	addr = *(unsigned long *)cmos;
	if((addr & 0xff000000) != 0xff000000) {
		printk(KERN_ERR "Voyager failed to get SUS mailbox (addr = 0x%lx\n", addr);
	} else {
		static struct resource res;
		
		res.name = "voyager SUS";
		res.start = addr;
		res.end = addr+0x3ff;
		
		request_resource(&iomem_resource, &res);
		voyager_SUS = (struct voyager_SUS *)
			ioremap(addr, 0x400);
		printk(KERN_NOTICE "Voyager SUS mailbox version 0x%x\n",
		       voyager_SUS->SUS_version);
		voyager_SUS->kernel_version = VOYAGER_MAILBOX_VERSION;
		voyager_SUS->kernel_flags = VOYAGER_OS_HAS_SYSINT;
	}

	/* clear the processor counts */
	voyager_extended_vic_processors = 0;
	voyager_quad_processors = 0;



	printk("VOYAGER: beginning CAT bus probe\n");
	/* set up the SuperSet Port Block which tells us where the
	 * CAT communication port is */
	sspb = inb(VOYAGER_SSPB_RELOCATION_PORT) * 0x100;
	VDEBUG(("VOYAGER DEBUG: sspb = 0x%x\n", sspb));

	/* now find out if were 8 slot or normal */
	if((inb(VIC_PROC_WHO_AM_I) & EIGHT_SLOT_IDENTIFIER)
	   == EIGHT_SLOT_IDENTIFIER) {
		voyager_8slot = 1;
		printk(KERN_NOTICE "Voyager: Eight slot 51xx configuration detected\n");
	}

	for(i = VOYAGER_MIN_MODULE;
	    i <= VOYAGER_MAX_MODULE; i++) {
		__u8 input;
		int asic;
		__u16 eprom_size;
		__u16 sp_offset;

		outb(VOYAGER_CAT_DESELECT, VOYAGER_CAT_CONFIG_PORT);
		outb(i, VOYAGER_CAT_CONFIG_PORT);

		/* check the presence of the module */
		outb(VOYAGER_CAT_RUN, CAT_CMD);
		outb(VOYAGER_CAT_IRCYC, CAT_CMD);
		outb(VOYAGER_CAT_HEADER, CAT_DATA);
		/* stream series of alternating 1's and 0's to stimulate
		 * response */
		outb(0xAA, CAT_DATA);
		input = inb(CAT_DATA);
		outb(VOYAGER_CAT_END, CAT_CMD);
		if(input != VOYAGER_CAT_HEADER) {
			continue;
		}
		CDEBUG(("VOYAGER DEBUG: found module id 0x%x, %s\n", i,
			cat_module_name(i)));
		*modpp = kmalloc(sizeof(voyager_module_t), GFP_KERNEL); /*&voyager_module_storage[cat_count++];*/
		if(*modpp == NULL) {
			printk("**WARNING** kmalloc failure in cat_init\n");
			continue;
		}
		memset(*modpp, 0, sizeof(voyager_module_t));
		/* need temporary asic for cat_subread.  It will be
		 * filled in correctly later */
		(*modpp)->asic = kmalloc(sizeof(voyager_asic_t), GFP_KERNEL); /*&voyager_asic_storage[asic_count];*/
		if((*modpp)->asic == NULL) {
			printk("**WARNING** kmalloc failure in cat_init\n");
			continue;
		}
		memset((*modpp)->asic, 0, sizeof(voyager_asic_t));
		(*modpp)->asic->asic_id = VOYAGER_CAT_ID;
		(*modpp)->asic->subaddr = VOYAGER_SUBADDR_HI;
		(*modpp)->module_addr = i;
		(*modpp)->scan_path_connected = 0;
		if(i == VOYAGER_PSI) {
			/* Exception leg for modules with no EEPROM */
			printk("Module \"%s\"\n", cat_module_name(i));
			continue;
		}
			       
		CDEBUG(("cat_init: Reading eeprom for module 0x%x at offset %d\n", i, VOYAGER_XSUM_END_OFFSET));
		outb(VOYAGER_CAT_RUN, CAT_CMD);
		cat_disconnect(*modpp, (*modpp)->asic);
		if(cat_subread(*modpp, (*modpp)->asic,
			       VOYAGER_XSUM_END_OFFSET, sizeof(eprom_size),
			       &eprom_size)) {
			printk("**WARNING**: Voyager couldn't read EPROM size for module 0x%x\n", i);
			outb(VOYAGER_CAT_END, CAT_CMD);
			continue;
		}
		if(eprom_size > sizeof(eprom_buf)) {
			printk("**WARNING**: Voyager insufficient size to read EPROM data, module 0x%x.  Need %d\n", i, eprom_size);
			outb(VOYAGER_CAT_END, CAT_CMD);
			continue;
		}
		outb(VOYAGER_CAT_END, CAT_CMD);
		outb(VOYAGER_CAT_RUN, CAT_CMD);
		CDEBUG(("cat_init: module 0x%x, eeprom_size %d\n", i, eprom_size));
		if(cat_subread(*modpp, (*modpp)->asic, 0, 
			       eprom_size, eprom_buf)) {
			outb(VOYAGER_CAT_END, CAT_CMD);
			continue;
		}
		outb(VOYAGER_CAT_END, CAT_CMD);
		printk("Module \"%s\", version 0x%x, tracer 0x%x, asics %d\n",
		       cat_module_name(i), eprom_hdr->version_id,
		       *((__u32 *)eprom_hdr->tracer),  eprom_hdr->num_asics);
		(*modpp)->ee_size = eprom_hdr->ee_size;
		(*modpp)->num_asics = eprom_hdr->num_asics;
		asicpp = &((*modpp)->asic);
		sp_offset = eprom_hdr->scan_path_offset;
		/* All we really care about are the Quad cards.  We
                 * identify them because they are in a processor slot
                 * and have only four asics */
		if((i < 0x10 || (i>=0x14 && i < 0x1c) || i>0x1f)) {
			modpp = &((*modpp)->next);
			continue;
		}
		/* Now we know it's in a processor slot, does it have
		 * a quad baseboard submodule */
		outb(VOYAGER_CAT_RUN, CAT_CMD);
		cat_read(*modpp, (*modpp)->asic, VOYAGER_SUBMODPRESENT,
			 &num_submodules);
		/* lowest two bits, active low */
		num_submodules = ~(0xfc | num_submodules);
		CDEBUG(("VOYAGER CAT: %d submodules present\n", num_submodules));
		if(num_submodules == 0) {
			/* fill in the dyadic extended processors */
			__u8 cpu = i & 0x07;

			printk("Module \"%s\": Dyadic Processor Card\n",
			       cat_module_name(i));
			voyager_extended_vic_processors |= (1<<cpu);
			cpu += 4;
			voyager_extended_vic_processors |= (1<<cpu);
			outb(VOYAGER_CAT_END, CAT_CMD);
			continue;
		}

		/* now we want to read the asics on the first submodule,
		 * which should be the quad base board */

		cat_read(*modpp, (*modpp)->asic, VOYAGER_SUBMODSELECT, &val);
		CDEBUG(("cat_init: SUBMODSELECT value = 0x%x\n", val));
		val = (val & 0x7c) | VOYAGER_QUAD_BASEBOARD;
		cat_write(*modpp, (*modpp)->asic, VOYAGER_SUBMODSELECT, val);

		outb(VOYAGER_CAT_END, CAT_CMD);
			 

		CDEBUG(("cat_init: Reading eeprom for module 0x%x at offset %d\n", i, VOYAGER_XSUM_END_OFFSET));
		outb(VOYAGER_CAT_RUN, CAT_CMD);
		cat_disconnect(*modpp, (*modpp)->asic);
		if(cat_subread(*modpp, (*modpp)->asic,
			       VOYAGER_XSUM_END_OFFSET, sizeof(eprom_size),
			       &eprom_size)) {
			printk("**WARNING**: Voyager couldn't read EPROM size for module 0x%x\n", i);
			outb(VOYAGER_CAT_END, CAT_CMD);
			continue;
		}
		if(eprom_size > sizeof(eprom_buf)) {
			printk("**WARNING**: Voyager insufficient size to read EPROM data, module 0x%x.  Need %d\n", i, eprom_size);
			outb(VOYAGER_CAT_END, CAT_CMD);
			continue;
		}
		outb(VOYAGER_CAT_END, CAT_CMD);
		outb(VOYAGER_CAT_RUN, CAT_CMD);
		CDEBUG(("cat_init: module 0x%x, eeprom_size %d\n", i, eprom_size));
		if(cat_subread(*modpp, (*modpp)->asic, 0, 
			       eprom_size, eprom_buf)) {
			outb(VOYAGER_CAT_END, CAT_CMD);
			continue;
		}
		outb(VOYAGER_CAT_END, CAT_CMD);
		/* Now do everything for the QBB submodule 1 */
		(*modpp)->ee_size = eprom_hdr->ee_size;
		(*modpp)->num_asics = eprom_hdr->num_asics;
		asicpp = &((*modpp)->asic);
		sp_offset = eprom_hdr->scan_path_offset;
		/* get rid of the dummy CAT asic and read the real one */
		kfree((*modpp)->asic);
		for(asic=0; asic < (*modpp)->num_asics; asic++) {
			int j;
			voyager_asic_t *asicp = *asicpp 
				= kmalloc(sizeof(voyager_asic_t), GFP_KERNEL); /*&voyager_asic_storage[asic_count++];*/
			voyager_sp_table_t *sp_table;
			voyager_at_t *asic_table;
			voyager_jtt_t *jtag_table;

			if(asicp == NULL) {
				printk("**WARNING** kmalloc failure in cat_init\n");
				continue;
			}
			memset(asicp, 0, sizeof(voyager_asic_t));
			asicpp = &(asicp->next);
			asicp->asic_location = asic;
			sp_table = (voyager_sp_table_t *)(eprom_buf + sp_offset);
			asicp->asic_id = sp_table->asic_id;
			asic_table = (voyager_at_t *)(eprom_buf + sp_table->asic_data_offset);
			for(j=0; j<4; j++)
				asicp->jtag_id[j] = asic_table->jtag_id[j];
			jtag_table = (voyager_jtt_t *)(eprom_buf + asic_table->jtag_offset);
			asicp->ireg_length = jtag_table->ireg_len;
			asicp->bit_location = (*modpp)->inst_bits;
			(*modpp)->inst_bits += asicp->ireg_length;
			if(asicp->ireg_length > (*modpp)->largest_reg)
				(*modpp)->largest_reg = asicp->ireg_length;
			if (asicp->ireg_length < (*modpp)->smallest_reg ||
			    (*modpp)->smallest_reg == 0)
				(*modpp)->smallest_reg = asicp->ireg_length;
			CDEBUG(("asic 0x%x, ireg_length=%d, bit_location=%d\n",
				asicp->asic_id, asicp->ireg_length,
				asicp->bit_location));
			if(asicp->asic_id == VOYAGER_QUAD_QABC) {
				CDEBUG(("VOYAGER CAT: QABC ASIC found\n"));
				qabc_asic = asicp;
			}
			sp_offset += sizeof(voyager_sp_table_t);
		}
		CDEBUG(("Module inst_bits = %d, largest_reg = %d, smallest_reg=%d\n",
			(*modpp)->inst_bits, (*modpp)->largest_reg,
			(*modpp)->smallest_reg));
		/* OK, now we have the QUAD ASICs set up, use them.
		 * we need to:
		 *
		 * 1. Find the Memory area for the Quad CPIs.
		 * 2. Find the Extended VIC processor
		 * 3. Configure a second extended VIC processor (This
		 *    cannot be done for the 51xx.
		 * */
		outb(VOYAGER_CAT_RUN, CAT_CMD);
		cat_connect(*modpp, (*modpp)->asic);
		CDEBUG(("CAT CONNECTED!!\n"));
		cat_subread(*modpp, qabc_asic, 0, sizeof(qabc_data), qabc_data);
		qic_addr = qabc_data[5] << 8;
		qic_addr = (qic_addr | qabc_data[6]) << 8;
		qic_addr = (qic_addr | qabc_data[7]) << 8;
		printk("Module \"%s\": Quad Processor Card; CPI 0x%lx, SET=0x%x\n",
		       cat_module_name(i), qic_addr, qabc_data[8]);
#if 0				/* plumbing fails---FIXME */
		if((qabc_data[8] & 0xf0) == 0) {
			/* FIXME: 32 way 8 CPU slot monster cannot be
			 * plumbed this way---need to check for it */

			printk("Plumbing second Extended Quad Processor\n");
			/* second VIC line hardwired to Quad CPU 1 */
			qabc_data[8] |= 0x20;
			cat_subwrite(*modpp, qabc_asic, 8, 1, &qabc_data[8]);
#ifdef VOYAGER_CAT_DEBUG
			/* verify plumbing */
			cat_subread(*modpp, qabc_asic, 8, 1, &qabc_data[8]);
			if((qabc_data[8] & 0xf0) == 0) {
				CDEBUG(("PLUMBING FAILED: 0x%x\n", qabc_data[8]));
			}
#endif
		}
#endif

		{
			struct resource *res = kmalloc(sizeof(struct resource),GFP_KERNEL);
			memset(res, 0, sizeof(struct resource));
			res->name = kmalloc(128, GFP_KERNEL);
			sprintf((char *)res->name, "Voyager %s Quad CPI", cat_module_name(i));
			res->start = qic_addr;
			res->end = qic_addr + 0x3ff;
			request_resource(&iomem_resource, res);
		}

		qic_addr = (unsigned long)ioremap(qic_addr, 0x400);
				
		for(j = 0; j < 4; j++) {
			__u8 cpu;

			if(voyager_8slot) {
				/* 8 slot has a different mapping,
				 * each slot has only one vic line, so
				 * 1 cpu in each slot must be < 8 */
				cpu = (i & 0x07) + j*8;
			} else {
				cpu = (i & 0x03) + j*4;
			}
			if( (qabc_data[8] & (1<<j))) {
				voyager_extended_vic_processors |= (1<<cpu);
			}
			if(qabc_data[8] & (1<<(j+4)) ) {
				/* Second SET register plumbed: Quad
				 * card has two VIC connected CPUs.
				 * Secondary cannot be booted as a VIC
				 * CPU */
				voyager_extended_vic_processors |= (1<<cpu);
				voyager_allowed_boot_processors &= (~(1<<cpu));
			}

			voyager_quad_processors |= (1<<cpu);
			voyager_quad_cpi_addr[cpu] = (struct voyager_qic_cpi *)
				(qic_addr+(j<<8));
			CDEBUG(("CPU%d: CPI address 0x%lx\n", cpu,
				(unsigned long)voyager_quad_cpi_addr[cpu]));
		}
		outb(VOYAGER_CAT_END, CAT_CMD);

		
		
		*asicpp = NULL;
		modpp = &((*modpp)->next);
	}
	*modpp = NULL;
	printk("CAT Bus Initialisation finished: extended procs 0x%x, quad procs 0x%x, allowed vic boot = 0x%x\n", voyager_extended_vic_processors, voyager_quad_processors, voyager_allowed_boot_processors);
	request_resource(&ioport_resource, &vic_res);
	if(voyager_quad_processors)
		request_resource(&ioport_resource, &qic_res);
	/* set up the front power switch */
}

int
voyager_cat_readb(__u8 module, __u8 asic, int reg)
{
	return 0;
}

static int
cat_disconnect(voyager_module_t *modp, voyager_asic_t *asicp) 
{
	__u8 val;
	int err = 0;

	if(!modp->scan_path_connected)
		return 0;
	if(asicp->asic_id != VOYAGER_CAT_ID) {
		CDEBUG(("cat_disconnect: ASIC is not CAT\n"));
		return 1;
	}
	err = cat_read(modp, asicp, VOYAGER_SCANPATH, &val);
	if(err) {
		CDEBUG(("cat_disconnect: failed to read SCANPATH\n"));
		return err;
	}
	val &= VOYAGER_DISCONNECT_ASIC;
	err = cat_write(modp, asicp, VOYAGER_SCANPATH, val);
	if(err) {
		CDEBUG(("cat_disconnect: failed to write SCANPATH\n"));
		return err;
	}
	outb(VOYAGER_CAT_END, CAT_CMD);
	outb(VOYAGER_CAT_RUN, CAT_CMD);
	modp->scan_path_connected = 0;

	return 0;
}

static int
cat_connect(voyager_module_t *modp, voyager_asic_t *asicp) 
{
	__u8 val;
	int err = 0;

	if(modp->scan_path_connected)
		return 0;
	if(asicp->asic_id != VOYAGER_CAT_ID) {
		CDEBUG(("cat_connect: ASIC is not CAT\n"));
		return 1;
	}

	err = cat_read(modp, asicp, VOYAGER_SCANPATH, &val);
	if(err) {
		CDEBUG(("cat_connect: failed to read SCANPATH\n"));
		return err;
	}
	val |= VOYAGER_CONNECT_ASIC;
	err = cat_write(modp, asicp, VOYAGER_SCANPATH, val);
	if(err) {
		CDEBUG(("cat_connect: failed to write SCANPATH\n"));
		return err;
	}
	outb(VOYAGER_CAT_END, CAT_CMD);
	outb(VOYAGER_CAT_RUN, CAT_CMD);
	modp->scan_path_connected = 1;

	return 0;
}

void
voyager_cat_power_off(void)
{
	/* Power the machine off by writing to the PSI over the CAT
         * bus */
	__u8 data;
	voyager_module_t psi = { 0 };
	voyager_asic_t psi_asic = { 0 };

	psi.asic = &psi_asic;
	psi.asic->asic_id = VOYAGER_CAT_ID;
	psi.asic->subaddr = VOYAGER_SUBADDR_HI;
	psi.module_addr = VOYAGER_PSI;
	psi.scan_path_connected = 0;

	outb(VOYAGER_CAT_END, CAT_CMD);
	/* Connect the PSI to the CAT Bus */
	outb(VOYAGER_CAT_DESELECT, VOYAGER_CAT_CONFIG_PORT);
	outb(VOYAGER_PSI, VOYAGER_CAT_CONFIG_PORT);
	outb(VOYAGER_CAT_RUN, CAT_CMD);
	cat_disconnect(&psi, &psi_asic);
	/* Read the status */
	cat_subread(&psi, &psi_asic, VOYAGER_PSI_GENERAL_REG, 1, &data);
	outb(VOYAGER_CAT_END, CAT_CMD);
	CDEBUG(("PSI STATUS 0x%x\n", data));
	/* These two writes are power off prep and perform */
	data = PSI_CLEAR;
	outb(VOYAGER_CAT_RUN, CAT_CMD);
	cat_subwrite(&psi, &psi_asic, VOYAGER_PSI_GENERAL_REG, 1, &data);
	outb(VOYAGER_CAT_END, CAT_CMD);
	data = PSI_POWER_DOWN;
	outb(VOYAGER_CAT_RUN, CAT_CMD);
	cat_subwrite(&psi, &psi_asic, VOYAGER_PSI_GENERAL_REG, 1, &data);
	outb(VOYAGER_CAT_END, CAT_CMD);
}

struct voyager_status voyager_status = { 0 };

void
voyager_cat_psi(__u8 cmd, __u16 reg, __u8 *data)
{
	voyager_module_t psi = { 0 };
	voyager_asic_t psi_asic = { 0 };

	psi.asic = &psi_asic;
	psi.asic->asic_id = VOYAGER_CAT_ID;
	psi.asic->subaddr = VOYAGER_SUBADDR_HI;
	psi.module_addr = VOYAGER_PSI;
	psi.scan_path_connected = 0;

	outb(VOYAGER_CAT_END, CAT_CMD);
	/* Connect the PSI to the CAT Bus */
	outb(VOYAGER_CAT_DESELECT, VOYAGER_CAT_CONFIG_PORT);
	outb(VOYAGER_PSI, VOYAGER_CAT_CONFIG_PORT);
	outb(VOYAGER_CAT_RUN, CAT_CMD);
	cat_disconnect(&psi, &psi_asic);
	switch(cmd) {
	case VOYAGER_PSI_READ:
		cat_read(&psi, &psi_asic, reg, data);
		break;
	case VOYAGER_PSI_WRITE:
		cat_write(&psi, &psi_asic, reg, *data);
		break;
	case VOYAGER_PSI_SUBREAD:
		cat_subread(&psi, &psi_asic, reg, 1, data);
		break;
	case VOYAGER_PSI_SUBWRITE:
		cat_subwrite(&psi, &psi_asic, reg, 1, data);
		break;
	default:
		printk(KERN_ERR "Voyager PSI, unrecognised command %d\n", cmd);
		break;
	}
	outb(VOYAGER_CAT_END, CAT_CMD);
}

void
voyager_cat_do_common_interrupt(void)
{
	/* This is caused either by a memory parity error or something
	 * in the PSI */
	__u8 data;
	voyager_module_t psi = { 0 };
	voyager_asic_t psi_asic = { 0 };
	struct voyager_psi psi_reg;
	int i;
 re_read:
	psi.asic = &psi_asic;
	psi.asic->asic_id = VOYAGER_CAT_ID;
	psi.asic->subaddr = VOYAGER_SUBADDR_HI;
	psi.module_addr = VOYAGER_PSI;
	psi.scan_path_connected = 0;

	outb(VOYAGER_CAT_END, CAT_CMD);
	/* Connect the PSI to the CAT Bus */
	outb(VOYAGER_CAT_DESELECT, VOYAGER_CAT_CONFIG_PORT);
	outb(VOYAGER_PSI, VOYAGER_CAT_CONFIG_PORT);
	outb(VOYAGER_CAT_RUN, CAT_CMD);
	cat_disconnect(&psi, &psi_asic);
	/* Read the status.  NOTE: Need to read *all* the PSI regs here
	 * otherwise the cmn int will be reasserted */
	for(i = 0; i < sizeof(psi_reg.regs); i++) {
		cat_read(&psi, &psi_asic, i, &((__u8 *)&psi_reg.regs)[i]);
	}
	outb(VOYAGER_CAT_END, CAT_CMD);
	if((psi_reg.regs.checkbit & 0x02) == 0) {
		psi_reg.regs.checkbit |= 0x02;
		cat_write(&psi, &psi_asic, 5, psi_reg.regs.checkbit);
		printk("VOYAGER RE-READ PSI\n");
		goto re_read;
	}
	outb(VOYAGER_CAT_RUN, CAT_CMD);
	for(i = 0; i < sizeof(psi_reg.subregs); i++) {
		/* This looks strange, but the PSI doesn't do auto increment
		 * correctly */
		cat_subread(&psi, &psi_asic, VOYAGER_PSI_SUPPLY_REG + i, 
			    1, &((__u8 *)&psi_reg.subregs)[i]); 
	}
	outb(VOYAGER_CAT_END, CAT_CMD);
#ifdef VOYAGER_CAT_DEBUG
	printk("VOYAGER PSI: ");
	for(i=0; i<sizeof(psi_reg.regs); i++)
		printk("%02x ", ((__u8 *)&psi_reg.regs)[i]);
	printk("\n           ");
	for(i=0; i<sizeof(psi_reg.subregs); i++)
		printk("%02x ", ((__u8 *)&psi_reg.subregs)[i]);
	printk("\n");
#endif
	if(psi_reg.regs.intstatus & PSI_MON) {
		/* switch off or power fail */

		if(psi_reg.subregs.supply & PSI_SWITCH_OFF) {
			if(voyager_status.switch_off) {
				printk(KERN_ERR "Voyager front panel switch turned off again---Immediate power off!\n");
				voyager_cat_power_off();
				/* not reached */
			} else {
				printk(KERN_ERR "Voyager front panel switch turned off\n");
				voyager_status.switch_off = 1;
				voyager_status.request_from_kernel = 1;
				up(&kvoyagerd_sem);
			}
			/* Tell the hardware we're taking care of the
			 * shutdown, otherwise it will power the box off
			 * within 3 seconds of the switch being pressed and,
			 * which is much more important to us, continue to 
			 * assert the common interrupt */
			data = PSI_CLR_SWITCH_OFF;
			outb(VOYAGER_CAT_RUN, CAT_CMD);
			cat_subwrite(&psi, &psi_asic, VOYAGER_PSI_SUPPLY_REG,
				     1, &data);
			outb(VOYAGER_CAT_END, CAT_CMD);
		} else {

			VDEBUG(("Voyager ac fail reg 0x%x\n",
				psi_reg.subregs.ACfail));
			if((psi_reg.subregs.ACfail & AC_FAIL_STAT_CHANGE) == 0) {
				/* No further update */
				return;
			}
#if 0
			/* Don't bother trying to find out who failed.
			 * FIXME: This probably makes the code incorrect on
			 * anything other than a 345x */
			for(i=0; i< 5; i++) {
				if( psi_reg.subregs.ACfail &(1<<i)) {
					break;
				}
			}
			printk(KERN_NOTICE "AC FAIL IN SUPPLY %d\n", i);
#endif
			/* DON'T do this: it shuts down the AC PSI 
			outb(VOYAGER_CAT_RUN, CAT_CMD);
			data = PSI_MASK_MASK | i;
			cat_subwrite(&psi, &psi_asic, VOYAGER_PSI_MASK,
				     1, &data);
			outb(VOYAGER_CAT_END, CAT_CMD);
			*/
			printk(KERN_ERR "Voyager AC power failure\n");
			outb(VOYAGER_CAT_RUN, CAT_CMD);
			data = PSI_COLD_START;
			cat_subwrite(&psi, &psi_asic, VOYAGER_PSI_GENERAL_REG,
				     1, &data);
			outb(VOYAGER_CAT_END, CAT_CMD);
			voyager_status.power_fail = 1;
			voyager_status.request_from_kernel = 1;
			up(&kvoyagerd_sem);
		}
		
		
	} else if(psi_reg.regs.intstatus & PSI_FAULT) {
		/* Major fault! */
		printk(KERN_ERR "Voyager PSI Detected major fault, immediate power off!\n");
		voyager_cat_power_off();
		/* not reached */
	} else if(psi_reg.regs.intstatus & (PSI_DC_FAIL | PSI_ALARM
					    | PSI_CURRENT | PSI_DVM
					    | PSI_PSCFAULT | PSI_STAT_CHG)) {
		/* other psi fault */

		printk(KERN_WARNING "Voyager PSI status 0x%x\n", data);
		/* clear the PSI fault */
		outb(VOYAGER_CAT_RUN, CAT_CMD);
		cat_write(&psi, &psi_asic, VOYAGER_PSI_STATUS_REG, 0);
		outb(VOYAGER_CAT_END, CAT_CMD);
	}
}

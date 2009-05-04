/*
 * eisa_enumerator.c - provide support for EISA adapters in PA-RISC machines
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Copyright (c) 2002 Daniel Engstrom <5116@telia.com>
 *
 */

#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>

#include <asm/eisa_bus.h>
#include <asm/eisa_eeprom.h>


/*
 * Todo:
 * 
 * PORT init with MASK attr and other size than byte
 * MEMORY with other decode than 20 bit
 * CRC stuff
 * FREEFORM stuff
 */

#define EPI 0xc80
#define NUM_SLOT 16
#define SLOT2PORT(x) (x<<12)


/* macros to handle unaligned accesses and 
 * byte swapping. The data in the EEPROM is
 * little-endian on the big-endian PAROSC */
#define get_8(x) (*(u_int8_t*)(x))

static inline u_int16_t get_16(const unsigned char *x)
{ 
	return (x[1] << 8) | x[0];
}

static inline u_int32_t get_32(const unsigned char *x)
{
	return (x[3] << 24) | (x[2] << 16) | (x[1] << 8) | x[0];
}

static inline u_int32_t get_24(const unsigned char *x)
{
	return (x[2] << 24) | (x[1] << 16) | (x[0] << 8);
}

static void print_eisa_id(char *s, u_int32_t id)
{
	char vendor[4];
	int rev;
	int device;
	
	rev = id & 0xff;
	id >>= 8;
	device = id & 0xff;
	id >>= 8;
	vendor[3] = '\0';
	vendor[2] = '@' + (id & 0x1f);
	id >>= 5;	
	vendor[1] = '@' + (id & 0x1f);
	id >>= 5;	
	vendor[0] = '@' + (id & 0x1f);
	id >>= 5;	
	
	sprintf(s, "%s%02X%02X", vendor, device, rev);
}
       
static int configure_memory(const unsigned char *buf, 
		       struct resource *mem_parent,
		       char *name)
{
	int len;
	u_int8_t c;
	int i;
	struct resource *res;
	
	len=0;
	
	for (i=0;i<HPEE_MEMORY_MAX_ENT;i++) {
		c = get_8(buf+len);
		
		if (NULL != (res = kmalloc(sizeof(struct resource), GFP_KERNEL))) {
			int result;
			
			res->name = name;
			res->start = mem_parent->start + get_24(buf+len+2);
			res->end = res->start + get_16(buf+len+5)*1024;
			res->flags = IORESOURCE_MEM;
			printk("memory %lx-%lx ", (unsigned long)res->start, (unsigned long)res->end);
			result = request_resource(mem_parent, res);
			if (result < 0) {
				printk("\n" KERN_ERR "EISA Enumerator: failed to claim EISA Bus address space!\n");
				return result;
			}
		}
		 	
		len+=7;      
	
		if (!(c & HPEE_MEMORY_MORE)) {
			break;
		}
	}
	
	return len;
}


static int configure_irq(const unsigned char *buf)
{
	int len;
	u_int8_t c;
	int i;
	
	len=0;
	
	for (i=0;i<HPEE_IRQ_MAX_ENT;i++) {
		c = get_8(buf+len);
		
		printk("IRQ %d ", c & HPEE_IRQ_CHANNEL_MASK);
		if (c & HPEE_IRQ_TRIG_LEVEL) {
			eisa_make_irq_level(c & HPEE_IRQ_CHANNEL_MASK);
		} else {
			eisa_make_irq_edge(c & HPEE_IRQ_CHANNEL_MASK);
		}
		
		len+=2; 
		/* hpux seems to allow for
		 * two bytes of irq data but only defines one of
		 * them, I think */
		if  (!(c & HPEE_IRQ_MORE)) {
			break;
		}
	}
	
	return len;
}


static int configure_dma(const unsigned char *buf)
{
	int len;
	u_int8_t c;
	int i;
	
	len=0;
	
	for (i=0;i<HPEE_DMA_MAX_ENT;i++) {
		c = get_8(buf+len);
		printk("DMA %d ", c&HPEE_DMA_CHANNEL_MASK);
		/* fixme: maybe initialize the dma channel withthe timing ? */
		len+=2;      
		if (!(c & HPEE_DMA_MORE)) {
			break;
		}
	}
	
	return len;
}

static int configure_port(const unsigned char *buf, struct resource *io_parent,
		     char *board)
{
	int len;
	u_int8_t c;
	int i;
	struct resource *res;
	int result;
	
	len=0;
	
	for (i=0;i<HPEE_PORT_MAX_ENT;i++) {
		c = get_8(buf+len);
		
		if (NULL != (res = kmalloc(sizeof(struct resource), GFP_KERNEL))) {
			res->name = board;
			res->start = get_16(buf+len+1);
			res->end = get_16(buf+len+1)+(c&HPEE_PORT_SIZE_MASK)+1;
			res->flags = IORESOURCE_IO;
			printk("ioports %lx-%lx ", (unsigned long)res->start, (unsigned long)res->end);
			result = request_resource(io_parent, res);
			if (result < 0) {
				printk("\n" KERN_ERR "EISA Enumerator: failed to claim EISA Bus address space!\n");
				return result;
			}
		}

		len+=3;      
		if (!(c & HPEE_PORT_MORE)) {
			break;
		}
	}
	
	return len;
}


/* byte 1 and 2 is the port number to write
 * and at byte 3 the value to write starts.
 * I assume that there are and- and or- masks
 * here when HPEE_PORT_INIT_MASK is set but I have 
 * not yet encountered this. */
static int configure_port_init(const unsigned char *buf)
{
	int len=0;
	u_int8_t c;
	
	while (len<HPEE_PORT_INIT_MAX_LEN) {
		int s=0;
		c = get_8(buf+len);
		
		switch (c & HPEE_PORT_INIT_WIDTH_MASK)  {
		 case HPEE_PORT_INIT_WIDTH_BYTE:
			s=1;
			if (c & HPEE_PORT_INIT_MASK) {
				printk("\n" KERN_WARNING "port_init: unverified mask attribute\n");
				outb((inb(get_16(buf+len+1) & 
					  get_8(buf+len+3)) | 
				      get_8(buf+len+4)), get_16(buf+len+1));
				      
			} else {
				outb(get_8(buf+len+3), get_16(buf+len+1));
				      
			}
			break;
		 case HPEE_PORT_INIT_WIDTH_WORD:
			s=2;
			if (c & HPEE_PORT_INIT_MASK) {
 				printk(KERN_WARNING "port_init: unverified mask attribute\n");
				       outw((inw(get_16(buf+len+1)) &
					     get_16(buf+len+3)) |
					    get_16(buf+len+5), 
					    get_16(buf+len+1));
			} else {
				outw(cpu_to_le16(get_16(buf+len+3)), get_16(buf+len+1));
			}
			break;
		 case HPEE_PORT_INIT_WIDTH_DWORD:
			s=4;
			if (c & HPEE_PORT_INIT_MASK) {
 				printk("\n" KERN_WARNING "port_init: unverified mask attribute\n");
				outl((inl(get_16(buf+len+1) &
					  get_32(buf+len+3)) |
				      get_32(buf+len+7)), get_16(buf+len+1));
			} else {
				outl(cpu_to_le32(get_32(buf+len+3)), get_16(buf+len+1));
			}

			break;
		 default:
			printk("\n" KERN_ERR "Invalid port init word %02x\n", c);
			return 0;
		}
		
		if (c & HPEE_PORT_INIT_MASK) {   
			s*=2;
		}
		
		len+=s+3;
		if (!(c & HPEE_PORT_INIT_MORE)) {
			break;
		}
	}
	
	return len;
}

static int configure_choise(const unsigned char *buf, u_int8_t *info)
{
	int len;
	
	/* theis record contain the value of the functions
	 * configuration choises and an info byte which 
	 * describes which other records to expect in this 
	 * function */
	len = get_8(buf);
	*info=get_8(buf+len+1);
	 
	return len+2;
}

static int configure_type_string(const unsigned char *buf) 
{
	int len;
	
	/* just skip past the type field */
	len = get_8(buf);
	if (len > 80) {
		printk("\n" KERN_ERR "eisa_enumerator: type info field too long (%d, max is 80)\n", len);
	}
	
	return 1+len;
}

static int configure_function(const unsigned char *buf, int *more) 
{
	/* the init field seems to be a two-byte field
	 * which is non-zero if there are an other function following
	 * I think it is the length of the function def 
	 */
	*more = get_16(buf);
	
	return 2;
}

static int parse_slot_config(int slot,
			     const unsigned char *buf,
			     struct eeprom_eisa_slot_info *es, 
			     struct resource *io_parent,
			     struct resource *mem_parent)
{
	int res=0;
	int function_len;
	unsigned int pos=0;
	unsigned int maxlen;
	int num_func=0;
	u_int8_t flags;
	int p0;
	
	char *board;
	int id_string_used=0;
	
	if (NULL == (board = kmalloc(8, GFP_KERNEL))) {
		return -1;
	}
	print_eisa_id(board, es->eisa_slot_id);
	printk(KERN_INFO "EISA slot %d: %s %s ", 
	       slot, board, es->flags&HPEE_FLAG_BOARD_IS_ISA ? "ISA" : "EISA");
	
	maxlen = es->config_data_length < HPEE_MAX_LENGTH ?
			 es->config_data_length : HPEE_MAX_LENGTH;
	while ((pos < maxlen) && (num_func <= es->num_functions)) {
		pos+=configure_function(buf+pos, &function_len); 
		
		if (!function_len) {
			break;
		}
		num_func++;
		p0 = pos;
		pos += configure_choise(buf+pos, &flags);

		if (flags & HPEE_FUNCTION_INFO_F_DISABLED) {
			/* function disabled, skip silently */
			pos = p0 + function_len;
			continue;
		}
		if (flags & HPEE_FUNCTION_INFO_CFG_FREE_FORM) {
			/* I have no idea how to handle this */
			printk("function %d have free-form confgiuration, skipping ",
				num_func);
			pos = p0 + function_len;
			continue;
		}

		/* the ordering of the sections need
		 * more investigation.
		 * Currently I think that memory comaed before IRQ
		 * I assume the order is LSB to MSB in the 
		 * info flags 
		 * eg type, memory, irq, dma, port, HPEE_PORT_init 
		 */

		if (flags & HPEE_FUNCTION_INFO_HAVE_TYPE) {
			pos += configure_type_string(buf+pos);
		}
		
		if (flags & HPEE_FUNCTION_INFO_HAVE_MEMORY) {
			id_string_used=1;
			pos += configure_memory(buf+pos, mem_parent, board);
		} 
		
		if (flags & HPEE_FUNCTION_INFO_HAVE_IRQ) {
			pos += configure_irq(buf+pos);
		} 
		
		if (flags & HPEE_FUNCTION_INFO_HAVE_DMA) {
			pos += configure_dma(buf+pos);
		} 
		
		if (flags & HPEE_FUNCTION_INFO_HAVE_PORT) {
			id_string_used=1;
			pos += configure_port(buf+pos, io_parent, board);
		} 
		
		if (flags &  HPEE_FUNCTION_INFO_HAVE_PORT_INIT) {
			pos += configure_port_init(buf+pos);
		}
		
		if (p0 + function_len < pos) {
			printk("\n" KERN_ERR "eisa_enumerator: function %d length mis-match "
			       "got %d, expected %d\n",
			       num_func, pos-p0, function_len);
			res=-1;
			break;
		}
		pos = p0 + function_len;
	}
	printk("\n");
	if (!id_string_used) {
		kfree(board);
	}
	
	if (pos != es->config_data_length) {
		printk(KERN_ERR "eisa_enumerator: config data length mis-match got %d, expected %d\n",
			pos, es->config_data_length);
		res=-1;
	}
	
	if (num_func != es->num_functions) {
		printk(KERN_ERR "eisa_enumerator: number of functions mis-match got %d, expected %d\n",
			num_func, es->num_functions);
		res=-2;
	}
	
	return res;
	
}

static int init_slot(int slot, struct eeprom_eisa_slot_info *es)
{
	unsigned int id;
	
	char id_string[8];
	
	if (!(es->slot_info&HPEE_SLOT_INFO_NO_READID)) {
		/* try to read the id of the board in the slot */
		id = le32_to_cpu(inl(SLOT2PORT(slot)+EPI));
		
		if (0xffffffff == id) {
			/* Maybe we didn't expect a card to be here... */
			if (es->eisa_slot_id == 0xffffffff)
				return -1;
			
			/* this board is not here or it does not 
			 * support readid 
			 */
			printk(KERN_ERR "EISA slot %d a configured board was not detected (", 
			       slot);
			
			print_eisa_id(id_string, es->eisa_slot_id);
			printk(" expected %s)\n", id_string);
		
			return -1;	

		}
		if (es->eisa_slot_id != id) {
			print_eisa_id(id_string, id);
			printk(KERN_ERR "EISA slot %d id mis-match: got %s", 
			       slot, id_string);
			
			print_eisa_id(id_string, es->eisa_slot_id);
			printk(" expected %s \n", id_string);
		
			return -1;	
			
		}
	}
	
	/* now: we need to enable the board if 
	 * it supports enabling and run through
	 * the port init sction if present
	 * and finally record any interrupt polarity
	 */
	if (es->slot_features & HPEE_SLOT_FEATURES_ENABLE) {
		/* enable board */
		outb(0x01| inb(SLOT2PORT(slot)+EPI+4),
		     SLOT2PORT(slot)+EPI+4);
	}
	
	return 0;
}


int eisa_enumerator(unsigned long eeprom_addr,
		    struct resource *io_parent, struct resource *mem_parent) 
{
	int i;
	struct eeprom_header *eh;
	static char eeprom_buf[HPEE_MAX_LENGTH];
	
	for (i=0; i < HPEE_MAX_LENGTH; i++) {
		eeprom_buf[i] = gsc_readb(eeprom_addr+i);
	}
	
	printk(KERN_INFO "Enumerating EISA bus\n");
		    	
	eh = (struct eeprom_header*)(eeprom_buf);
	for (i=0;i<eh->num_slots;i++) {
		struct eeprom_eisa_slot_info *es;
		
		es = (struct eeprom_eisa_slot_info*)
			(&eeprom_buf[HPEE_SLOT_INFO(i)]);
	        
		if (-1==init_slot(i+1, es)) {
			continue;
		}
		
		if (es->config_data_offset < HPEE_MAX_LENGTH) {
			if (parse_slot_config(i+1, &eeprom_buf[es->config_data_offset],
					      es, io_parent, mem_parent)) {
				return -1;
			}
		} else {
			printk (KERN_WARNING "EISA EEPROM offset 0x%x out of range\n",es->config_data_offset);
			return -1;
		}
	}
	return eh->num_slots;
}


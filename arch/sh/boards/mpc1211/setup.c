/*
 * linux/arch/sh/boards/mpc1211/setup.c
 *
 * Copyright (C) 2002  Saito.K & Jeanne,  Fujii.Y
 *
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/machvec.h>
#include <asm/mpc1211/mpc1211.h>
#include <asm/mpc1211/pci.h>
#include <asm/mpc1211/m1543c.h>

/* ALI15X3 SMBus address offsets */
#define SMBHSTSTS   (0 + 0x3100)
#define SMBHSTCNT   (1 + 0x3100)
#define SMBHSTSTART (2 + 0x3100)
#define SMBHSTCMD   (7 + 0x3100)
#define SMBHSTADD   (3 + 0x3100)
#define SMBHSTDAT0  (4 + 0x3100)
#define SMBHSTDAT1  (5 + 0x3100)
#define SMBBLKDAT   (6 + 0x3100)

/* Other settings */
#define MAX_TIMEOUT 500		/* times 1/100 sec */

/* ALI15X3 command constants */
#define ALI15X3_ABORT      0x04
#define ALI15X3_T_OUT      0x08
#define ALI15X3_QUICK      0x00
#define ALI15X3_BYTE       0x10
#define ALI15X3_BYTE_DATA  0x20
#define ALI15X3_WORD_DATA  0x30
#define ALI15X3_BLOCK_DATA 0x40
#define ALI15X3_BLOCK_CLR  0x80

/* ALI15X3 status register bits */
#define ALI15X3_STS_IDLE	0x04
#define ALI15X3_STS_BUSY	0x08
#define ALI15X3_STS_DONE	0x10
#define ALI15X3_STS_DEV		0x20	/* device error */
#define ALI15X3_STS_COLL	0x40	/* collision or no response */
#define ALI15X3_STS_TERM	0x80	/* terminated by abort */
#define ALI15X3_STS_ERR		0xE0	/* all the bad error bits */

static void __init pci_write_config(unsigned long busNo,
				    unsigned long devNo,
				    unsigned long fncNo,
				    unsigned long cnfAdd,
				    unsigned long cnfData)
{
	ctrl_outl((0x80000000 
                + ((busNo & 0xff) << 16) 
                + ((devNo & 0x1f) << 11) 
                + ((fncNo & 0x07) <<  8) 
		+ (cnfAdd & 0xfc)), PCIPAR);

        ctrl_outl(cnfData, PCIPDR);
}

/*
  Initialize IRQ setting
*/

static unsigned char m_irq_mask = 0xfb;
static unsigned char s_irq_mask = 0xff;

static void disable_mpc1211_irq(unsigned int irq)
{
	if( irq < 8) {
		m_irq_mask |= (1 << irq);
		outb(m_irq_mask,I8259_M_MR);
	} else {
		s_irq_mask |= (1 << (irq - 8));
		outb(s_irq_mask,I8259_S_MR);
	}

}

static void enable_mpc1211_irq(unsigned int irq)
{
	if( irq < 8) {
		m_irq_mask &= ~(1 << irq);
		outb(m_irq_mask,I8259_M_MR);
	} else {
		s_irq_mask &= ~(1 << (irq - 8));
		outb(s_irq_mask,I8259_S_MR);
	}
}

static inline int mpc1211_irq_real(unsigned int irq)
{
	int value;
	int irqmask;

	if ( irq < 8) {
		irqmask = 1<<irq;
		outb(0x0b,I8259_M_CR);		/* ISR register */
		value = inb(I8259_M_CR) & irqmask;
		outb(0x0a,I8259_M_CR);		/* back ro the IPR reg */
		return value;
	}
	irqmask = 1<<(irq - 8);
	outb(0x0b,I8259_S_CR);		/* ISR register */
	value = inb(I8259_S_CR) & irqmask;
	outb(0x0a,I8259_S_CR);		/* back ro the IPR reg */
	return value;
}

static void mask_and_ack_mpc1211(unsigned int irq)
{
	if(irq < 8) {
		if(m_irq_mask & (1<<irq)){
		  if(!mpc1211_irq_real(irq)){
		    atomic_inc(&irq_err_count)
		    printk("spurious 8259A interrupt: IRQ %x\n",irq);
		   }
		} else {
			m_irq_mask |= (1<<irq);
		}
		inb(I8259_M_MR);		/* DUMMY */
		outb(m_irq_mask,I8259_M_MR);	/* disable */
		outb(0x60+irq,I8259_M_CR);	/* EOI */
		
	} else {
		if(s_irq_mask & (1<<(irq - 8))){
		  if(!mpc1211_irq_real(irq)){
		    atomic_inc(&irq_err_count);
		    printk("spurious 8259A interrupt: IRQ %x\n",irq);
		  }
		} else {
			s_irq_mask |= (1<<(irq - 8));
		}
		inb(I8259_S_MR);		/* DUMMY */
		outb(s_irq_mask,I8259_S_MR);	/* disable */
		outb(0x60+(irq-8),I8259_S_CR); 	/* EOI */
		outb(0x60+2,I8259_M_CR);
	}
}

static void end_mpc1211_irq(unsigned int irq)
{
	enable_mpc1211_irq(irq);
}

static unsigned int startup_mpc1211_irq(unsigned int irq)
{
	enable_mpc1211_irq(irq);
	return 0;
}

static void shutdown_mpc1211_irq(unsigned int irq)
{
	disable_mpc1211_irq(irq);
}

static struct hw_interrupt_type mpc1211_irq_type = {
	.typename	= "MPC1211-IRQ",
	.startup	= startup_mpc1211_irq,
	.shutdown	= shutdown_mpc1211_irq,
	.enable		= enable_mpc1211_irq,
	.disable	= disable_mpc1211_irq,
	.ack		= mask_and_ack_mpc1211,
	.end		= end_mpc1211_irq
};

static void make_mpc1211_irq(unsigned int irq)
{
	irq_desc[irq].chip = &mpc1211_irq_type;
	irq_desc[irq].status  = IRQ_DISABLED;
	irq_desc[irq].action  = 0;
	irq_desc[irq].depth   = 1;
	disable_mpc1211_irq(irq);
}

int mpc1211_irq_demux(int irq)
{
	unsigned int poll;

	if( irq == 2 ) {
		outb(0x0c,I8259_M_CR);
		poll = inb(I8259_M_CR);
		if(poll & 0x80) {
			irq = (poll & 0x07);
		}
		if( irq == 2) {
			outb(0x0c,I8259_S_CR);
			poll = inb(I8259_S_CR);
			irq = (poll & 0x07) + 8;
		}
	}
	return irq;
}

static void __init init_mpc1211_IRQ(void)
{
	int i;
	/*
	 * Super I/O (Just mimic PC):
	 *  1: keyboard
	 *  3: serial 1
	 *  4: serial 0
	 *  5: printer
	 *  6: floppy
	 *  8: rtc
	 * 10: lan
	 * 12: mouse
	 * 14: ide0
	 * 15: ide1
	 */

	pci_write_config(0,0,0,0x54, 0xb0b0002d);
	outb(0x11, I8259_M_CR); 	/* mater icw1 edge trigger  */
	outb(0x11, I8259_S_CR);		/* slave icw1 edge trigger  */
	outb(0x20, I8259_M_MR); 	/* m icw2 base vec 0x08	    */
	outb(0x28, I8259_S_MR);		/* s icw2 base vec 0x70	    */
	outb(0x04, I8259_M_MR);		/* m icw3 slave irq2	    */
	outb(0x02, I8259_S_MR);		/* s icw3 slave id	    */
	outb(0x01, I8259_M_MR);		/* m icw4 non buf normal eoi*/
	outb(0x01, I8259_S_MR);		/* s icw4 non buf normal eo1*/
	outb(0xfb, I8259_M_MR);		/* disable irq0--irq7  */
	outb(0xff, I8259_S_MR);		/* disable irq8--irq15 */

	for ( i=0; i < 16; i++) {
		if(i != 2) {
			make_mpc1211_irq(i);
		}
	}
}

static void delay1000(void)
{
	int i;

	for (i=0; i<1000; i++)
		ctrl_delay();
}

static int put_smb_blk(unsigned char *p, int address, int command, int no)
{
	int temp;
	int timeout;
	int i;

	outb(0xff, SMBHSTSTS);
	temp = inb(SMBHSTSTS);
	for (timeout = 0; (timeout < MAX_TIMEOUT) && !(temp & ALI15X3_STS_IDLE); timeout++) {
		delay1000();
		temp = inb(SMBHSTSTS);
	}
	if (timeout >= MAX_TIMEOUT){
		return -1;
	}

	outb(((address & 0x7f) << 1), SMBHSTADD);
	outb(0xc0, SMBHSTCNT);
	outb(command & 0xff, SMBHSTCMD);
	outb(no & 0x1f, SMBHSTDAT0);

	for(i = 1; i <= no; i++) {
		outb(*p++, SMBBLKDAT);
	}
	outb(0xff, SMBHSTSTART);

	temp = inb(SMBHSTSTS);
	for (timeout = 0; (timeout < MAX_TIMEOUT) && !(temp & (ALI15X3_STS_ERR | ALI15X3_STS_DONE)); timeout++) {
		delay1000();
		temp = inb(SMBHSTSTS);
	}
	if (timeout >= MAX_TIMEOUT) {
		return -2;
	}
	if ( temp & ALI15X3_STS_ERR ){
		return -3;
	}
	return 0;
}

static struct resource heartbeat_resources[] = {
	[0] = {
		.start	= 0xa2000000,
		.end	= 0xa2000000,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(heartbeat_resources),
	.resource	= heartbeat_resources,
};

static struct platform_device *mpc1211_devices[] __initdata = {
	&heartbeat_device,
};

static int __init mpc1211_devices_setup(void)
{
	return platform_add_devices(mpc1211_devices,
				    ARRAY_SIZE(mpc1211_devices));
}
__initcall(mpc1211_devices_setup);

/* arch/sh/boards/mpc1211/rtc.c */
void mpc1211_time_init(void);

static void __init mpc1211_setup(char **cmdline_p)
{
	unsigned char spd_buf[128];

	__set_io_port_base(PA_PCI_IO);

	pci_write_config(0,0,0,0x54, 0xb0b00000);

	do {
		outb(ALI15X3_ABORT, SMBHSTCNT);
		spd_buf[0] = 0x0c;
		spd_buf[1] = 0x43;
		spd_buf[2] = 0x7f;
		spd_buf[3] = 0x03;
		spd_buf[4] = 0x00;
		spd_buf[5] = 0x03;
		spd_buf[6] = 0x00;
	} while (put_smb_blk(spd_buf, 0x69, 0, 7) < 0);

	board_time_init = mpc1211_time_init;

	return 0;
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_mpc1211 __initmv = {
	.mv_name		= "Interface MPC-1211(CTP/PCI/MPC-SH02)",
	.mv_setup		= mpc1211_setup,
	.mv_nr_irqs		= 48,
	.mv_irq_demux		= mpc1211_irq_demux,
	.mv_init_irq		= init_mpc1211_IRQ,
};

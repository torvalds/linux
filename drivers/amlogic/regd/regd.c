/*
 * Amlogic Register Debug Driver.
 *
 * Author: Zhihua Xie <zhihua.xie@amlogic.com>
 *
 * Copyright (C) 2010-2013 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Standard Linux headers */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/delay.h>

#include <plat/io.h>
#include <mach/io.h>
#include <mach/register.h>
#include <mach/clock.h>

 /* Amlogic headers */
#include "regd.h"

#define DREG_DEV_NAME		"regd"
#define DREG_CLS_NAME		"register"

/* device driver variables */
static dev_t regd_devt;
static struct class *regd_clsp;
static struct cdev regd_cdev;
static struct device *regd_devp;

#define  GAMMA_VCOM_POL		7	 //RW
#define  GAMMA_RVS_OUT		6	 //RW
#define  ADR_RDY		5	 //Read Only
#define  WR_RDY 		4	 //Read Only
#define  RD_RDY 		3	 //Read Only
#define  GAMMA_TR		2	 //RW
#define  GAMMA_SET		1	 //RW
#define  GAMMA_EN		0	 //RWs

#define H_RD			12
#define H_AUTO_INC		11
#define H_SEL_R			10
#define H_SEL_G			9
#define H_SEL_B			8
#define HADR_MSB		7	//7:0
#define HADR			0	//7:0



#define reg_read( _reg )  aml_read_reg32( (_reg) )
#define reg_write( _reg, _value)  aml_write_reg32(  (_reg), (_value))
#define bits_write( _reg, _value, _start, _len)  \
	aml_set_reg32_bits( (_reg),(_value), (_start), (_len))
#define bits_read(_reg, _start, _len)  aml_get_reg32_bits((_reg),(_start),(_len))


static void set_lcd_gamma_table(u16 *data, u32 rgb_mask)
{
	int i = 0;

	while (!(READ_CBUS_REG(L_GAMMA_CNTL_PORT) & (0x1 << ADR_RDY)));
		WRITE_CBUS_REG(L_GAMMA_ADDR_PORT, (0x1 << H_AUTO_INC) |
						    (0x1 << rgb_mask)   |
						    (0x0 << HADR));
	for (i = 0;i < 256; i++) {
		while (!( READ_CBUS_REG(L_GAMMA_CNTL_PORT) & (0x1 << WR_RDY) )) ;
			WRITE_CBUS_REG(L_GAMMA_DATA_PORT, data[i]);
	}
	while (!(READ_CBUS_REG(L_GAMMA_CNTL_PORT) & (0x1 << ADR_RDY)));
	WRITE_CBUS_REG(L_GAMMA_ADDR_PORT, (0x1 << H_AUTO_INC) |
					    (0x1 << rgb_mask)   |
					    (0x23 << HADR));
}

static ssize_t reg_show(struct class *cls,
			struct class_attribute *attr,
			char *buf)
{
	/* show help */
	pr_info("Usage:");
	pr_info("	echo rc|rp|rx|rh address > /sys/class/register/reg\n");
	pr_info("	echo wc|wp|wx|wh address value > /sys/class/register/reg\n");
	pr_info("Notes:");
	pr_info("	r:read w:write c:CBUS p:APB x:AXI h:AHB \n");
	return 0;
}

static ssize_t reg_store(struct class *cls,
			 struct class_attribute *attr,
			 const char *buffer, size_t count)
{

	int n = 0;
	char *buf_orig, *ps, *token;
	char *parm[4];
	unsigned int addr = 0, val = 0, retval = 0;

	buf_orig = kstrdup(buffer, GFP_KERNEL);

	ps = buf_orig;
	while (1) {
		token = strsep(&ps, " \n");
		if (token == NULL)
			break;
		if (*token == '\0')
			continue;
		parm[n++] = token;
	}

	if ((parm[0][0] == 'r')) {
		if (n != 2) {
			printk(KERN_WARNING"read: invalid parameter\n");
			pr_info("please: cat /sys/class/register/reg \n");
			kfree(buf_orig);
			return count;
		}
		addr = simple_strtol(parm[1], NULL, 16);
		//pr_info("%s 0x%x\n", parm[0], addr);
		switch (parm[0][1]) {
		case 'c':
			retval = reg_read(CBUS_REG_ADDR(addr));
			break;
		case 'p':
			retval = reg_read(APB_REG_ADDR(addr));
			break;
		case 'a':
			addr = addr << 2;
			retval = reg_read(APB_REG_ADDR(addr));
			break;
		case 'x':
			retval = reg_read(AXI_REG_ADDR(addr));
			break;
		case 'h':
			retval = reg_read(AHB_REG_ADDR(addr));
			break;
		case 'o':
			retval = reg_read(AOBUS_REG_ADDR(addr));
			break;
		default:
			break;
		}
		printk(KERN_WARNING"%s: 0x%x --> 0x%x\n", parm[0], addr, retval);
	} else if ((parm[0][0] == 'w')) {
		if (n != 3) {
			printk(KERN_WARNING"read: invalid parameter\n");
			pr_info("please: cat /sys/class/register/reg \n");
			kfree(buf_orig);
			return count;
		}

		addr = simple_strtol(parm[1], NULL, 16);
		val = simple_strtol(parm[2], NULL, 16);
		if (parm[0][1] != 'c') {
			printk(KERN_WARNING"%s 0x%x 0x%x", parm[0], addr, val);
		}

		switch (parm[0][1]) {
		case 'c':
			reg_write(CBUS_REG_ADDR(addr), val);
			retval = reg_read(CBUS_REG_ADDR(addr));
			break;
		case 'p':
			reg_write(APB_REG_ADDR(addr), val);
			retval = reg_read(APB_REG_ADDR(addr));
			break;
		case 'a':
			addr = addr << 2;
			reg_write(APB_REG_ADDR(addr), val);
			retval = reg_read(APB_REG_ADDR(addr));
			break;
		case 'x':
			reg_write(AXI_REG_ADDR(addr), val);
			retval = reg_read(AXI_REG_ADDR(addr));
			break;
		case 'h':
			reg_write(AHB_REG_ADDR(addr), val);
			retval = reg_read(AHB_REG_ADDR(addr));
			break;
		case 'o':
			reg_write(AOBUS_REG_ADDR(addr), val);
			retval = reg_read(AOBUS_REG_ADDR(addr));
			break;
		default:
			break;
		}
		printk(KERN_WARNING"%s: 0x%x <-- 0x%x\n", parm[0], addr, retval);
	} else {
		printk(KERN_WARNING"invalid command\n");
		printk(KERN_WARNING"please: cat /sys/class/register/reg \n");
	}

	kfree(buf_orig);
	return count;

}

static CLASS_ATTR(reg, S_IWUSR | S_IRUGO, reg_show, reg_store);

static ssize_t bit_show(struct class *cls,
			struct class_attribute *attr,
			char *buf)
{
	pr_info("Usage:");
	pr_info("	echo wcb/wab addr bits val > /sys/class/register/bit \n");
	pr_info("Notes:");
	pr_info("	r:read w:write c:CBUS a:AOBUS\n");
	return 0;
}

static ssize_t bit_store(struct class *cls,
			 struct class_attribute *attr,
			 const char *buffer, size_t count)
{
	int n = 0;
	char *buf_orig, *ps, *token;
	char *parm[4];
	u32 addr, val, bit;

	buf_orig = kstrdup(buffer, GFP_KERNEL);
	ps = buf_orig;
	while (1) {
		token = strsep(&ps, " \n");
		if (token == NULL)
			break;
		if (*token == '\0')
			continue;
		parm[n++] = token;
	}

	if ((parm[0][0] == 'w') && parm[0][1] == 'c' && parm[0][2] == 'b') {
		//ret = sscanf(buffer, "%x %x %x", &addr, &bit, &val);
		addr = simple_strtol(parm[1], NULL, 16);
		bit = simple_strtol(parm[2], NULL, 10);
		val = simple_strtol(parm[3], NULL, 16);
		bits_write(CBUS_REG_ADDR(addr),val, bit, 1);
	}else if ((parm[0][0] == 'w') && parm[0][1] == 'a' && parm[0][2] == 'b') {
		//ret = sscanf(buffer, "%x %x %x", &addr, &bit, &val);
		addr = simple_strtol(parm[1], NULL, 16);
		bit = simple_strtol(parm[2], NULL, 10);
		val = simple_strtol(parm[3], NULL, 16);
		WRITE_AOBUS_REG_BITS(addr,val,bit,1);
	}

	else {
		pr_info("invalid command\n");
		pr_info("please: cat /sys/class/register/bit");
	}
	kfree(buf_orig);
	return count;

}

static CLASS_ATTR(bit, S_IWUSR | S_IRUGO, bit_show, bit_store);

static ssize_t gamma_show(struct class *cls,
			struct class_attribute *attr,
			char *buf)
{
	pr_info("Usage:");
	pr_info("	echo sgr|sgg|sgb xxx...xx > /sys/class/register/gamma\n");
	pr_info("Notes:");
	pr_info("	if the string xxx......xx is less than 256*3,");
	pr_info("	then the remaining will be set value 0\n");
	pr_info("	if the string xxx......xx is more than 256*3, ");
	pr_info("	then the remaining will be ignored\n");
	return 0;
}


static ssize_t gamma_store(struct class *cls,
			 struct class_attribute *attr,
			 const char *buffer, size_t count)
{

	int n = 0;
	char *buf_orig, *ps, *token;
	char *parm[4];
	unsigned short *gammaR, *gammaG, *gammaB;
	unsigned int gamma_count;
	char gamma[4];
	int i = 0;

	/* to avoid the bellow warning message while compiling:
	 * warning: the frame size of 1576 bytes is larger than 1024 bytes
	 */
	gammaR = kmalloc(256 * sizeof(unsigned short), GFP_KERNEL);
	gammaG = kmalloc(256 * sizeof(unsigned short), GFP_KERNEL);
	gammaB = kmalloc(256 * sizeof(unsigned short), GFP_KERNEL);

	buf_orig = kstrdup(buffer, GFP_KERNEL);
	ps = buf_orig;
	while (1) {
		token = strsep(&ps, " \n");
		if (token == NULL)
			break;
		if (*token == '\0')
			continue;
		parm[n++] = token;
	}

	if ((parm[0][0] == 's') && (parm[0][1] == 'g')) {
		memset(gammaR, 0, 256 * sizeof(unsigned short));
		gamma_count = (strlen(parm[1]) + 2) / 3;
		if (gamma_count > 256)
			gamma_count = 256;

		for (i = 0; i < gamma_count; ++i) {
			gamma[0] = parm[1][3 * i + 0];
			gamma[1] = parm[1][3 * i + 1];
			gamma[2] = parm[1][3 * i + 2];
			gamma[3] = '\0';
			gammaR[i] = simple_strtol(gamma, NULL, 16);
		}

		switch (parm[0][2]) {
		case 'r':
			set_lcd_gamma_table(gammaR, H_SEL_R);
			break;

		case 'g':
			set_lcd_gamma_table(gammaR, H_SEL_G);
			break;

		case 'b':
			set_lcd_gamma_table(gammaR, H_SEL_B);
			break;
		default:
			break;
		}
	} else {
		pr_info("invalid command\n");
		pr_info("please: cat /sys/class/register/gamma");

	}
	kfree(buf_orig);
	kfree(gammaR);
	kfree(gammaG);
	kfree(gammaB);
	return count;
}

static CLASS_ATTR(gamma, S_IWUSR | S_IRUGO, gamma_show, gamma_store);


static ssize_t cm2_show(struct class *cls,
			struct class_attribute *attr,
			char *buf)
{
	pr_info("Usage:");
	pr_info("	echo wm addr data0 data1 data2 data3 data4 > /sys/class/regsiter/cm2 \n");
	pr_info("	echo rm addr > /sys/class/regsiter/cm2 \n");
	return 0;
}

static ssize_t cm2_store(struct class *cls,
			 struct class_attribute *attr,
			 const char *buffer, size_t count)
{
	int n = 0;
	char *buf_orig, *ps, *token;
	char *parm[7];
	u32 addr;
	int data[5] = {0};
	unsigned int addr_port = 0x1d70;
	unsigned int data_port = 0x1d71;

	buf_orig = kstrdup(buffer, GFP_KERNEL);
	ps = buf_orig;
	while (1) {
		token = strsep(&ps, " \n");
		if (token == NULL)
			break;
		if (*token == '\0')
			continue;
		parm[n++] = token;
	}

	if ((parm[0][0] == 'w') && parm[0][1] == 'm' ) {
		if (n != 7) {
			pr_info("read: invalid parameter\n");
			pr_info("please: cat /sys/class/regsiter/cm2 \n");
			kfree(buf_orig);
			return count;
		}
		addr = simple_strtol(parm[1], NULL, 16);
		addr = addr - addr%8;
		data[0] = simple_strtol(parm[2], NULL, 16);
		data[1] = simple_strtol(parm[3], NULL, 16);
		data[2] = simple_strtol(parm[4], NULL, 16);
		data[3] = simple_strtol(parm[5], NULL, 16);
		data[4] = simple_strtol(parm[6], NULL, 16);

		reg_write(CBUS_REG_ADDR(addr_port), addr);
		reg_write(CBUS_REG_ADDR(data_port), data[0]);
		reg_write(CBUS_REG_ADDR(addr_port), addr + 1);
		reg_write(CBUS_REG_ADDR(data_port), data[1]);
		reg_write(CBUS_REG_ADDR(addr_port), addr + 2);
		reg_write(CBUS_REG_ADDR(data_port), data[2]);
		reg_write(CBUS_REG_ADDR(addr_port), addr + 3);
		reg_write(CBUS_REG_ADDR(data_port), data[3]);
		reg_write(CBUS_REG_ADDR(addr_port), addr + 4);
		reg_write(CBUS_REG_ADDR(data_port), data[4]);

		pr_info("wm: [0x%x] <-- 0x0 \n",addr);
	}
	else if ((parm[0][0] == 'r') && parm[0][1] == 'm' ) {
		if (n != 2) {
			pr_info("read: invalid parameter\n");
			pr_info("please: cat /sys/class/regsiter/cm2 \n");
			kfree(buf_orig);
			return count;
		}
		addr = simple_strtol(parm[1], NULL, 16);
		addr = addr - addr%8;
		reg_write(CBUS_REG_ADDR(addr_port), addr);
		data[0] = reg_read(CBUS_REG_ADDR(data_port));
		data[0] = reg_read(CBUS_REG_ADDR(data_port));
		data[0] = reg_read(CBUS_REG_ADDR(data_port));
		reg_write(CBUS_REG_ADDR(addr_port), addr+1);
		data[1] = reg_read(CBUS_REG_ADDR(data_port));
		data[1] = reg_read(CBUS_REG_ADDR(data_port));
		data[1] = reg_read(CBUS_REG_ADDR(data_port));
		reg_write(CBUS_REG_ADDR(addr_port), addr+2);
		data[2] = reg_read(CBUS_REG_ADDR(data_port));
		data[2] = reg_read(CBUS_REG_ADDR(data_port));
		data[2] = reg_read(CBUS_REG_ADDR(data_port));
		reg_write(CBUS_REG_ADDR(addr_port), addr+3);
		data[3] = reg_read(CBUS_REG_ADDR(data_port));
		data[3] = reg_read(CBUS_REG_ADDR(data_port));
		data[3] = reg_read(CBUS_REG_ADDR(data_port));
		reg_write(CBUS_REG_ADDR(addr_port), addr+4);
		data[4] = reg_read(CBUS_REG_ADDR(data_port));
		data[4] = reg_read(CBUS_REG_ADDR(data_port));
		data[4] = reg_read(CBUS_REG_ADDR(data_port));

		pr_info("rm:[0x%x]-->[0x%x][0x%x][0x%x][0x%x][0x%x] \n",addr, data[0],data[1],data[2],data[3],data[4]);
	}
	else {
		pr_info("invalid command\n");
		pr_info("please: cat /sys/class/register/bit");
	}
	kfree(buf_orig);
	return count;

}

static CLASS_ATTR(cm2, S_IWUSR | S_IRUGO, cm2_show, cm2_store);

// uS_gate_time_nr is the power of 2
unsigned int clk_util_clk_msr_pow(unsigned int clk_mux, unsigned int uS_gate_time_pow)
{
	unsigned int  msr;
	unsigned int regval = 0;
	aml_write_reg32(P_MSR_CLK_REG0,0);
	// Set the measurement gate to 64uS
	//clrsetbits_le32(P_MSR_CLK_REG0,0xffff,64-1);
	pr_info("Measured gate time: %u\n", 1<<uS_gate_time_pow);
	clrsetbits_le32(P_MSR_CLK_REG0, 0xffff, (1<<uS_gate_time_pow)-1);
	// Disable continuous measurement
	// disable interrupts
	clrbits_le32(P_MSR_CLK_REG0,((1 << 18) | (1 << 17)));
	clrsetbits_le32(P_MSR_CLK_REG0,(0x1f<<20),(clk_mux<<20)|(1<<19)|(1<<16));

	aml_read_reg32(P_MSR_CLK_REG0);
	// Wait for the measurement to be done
	do {
		regval = aml_read_reg32(P_MSR_CLK_REG0);
	} while (regval & (1 << 31));
	// disable measuring
	clrbits_le32(P_MSR_CLK_REG0,(1 << 16));

	msr=(aml_read_reg32(P_MSR_CLK_REG2)+31)&0x000FFFFF;
	pr_info("Measured 20bit value(+31): %u\n", msr);

	// Return value in MHz*measured_val
	return (msr>>uS_gate_time_pow)*1000000;
}

void test_ring_oscillator(unsigned long freq_sel_bit_lsb,
			  unsigned long osc_en_bit,
			  unsigned long osc_msr_mux_sel )
{
    int i;
    unsigned long freq_msr[4];

    for( i = 0; i < 4; i++ ) {

        // disable the HS_HVT ring oscillator
        aml_write_reg32(P_AM_RING_OSC_REG0, aml_read_reg32(P_AM_RING_OSC_REG0) & ~(1 << osc_en_bit) );

        // Select the frequency: 0 = slowest, 3 = fastest
        aml_write_reg32(P_AM_RING_OSC_REG0, (aml_read_reg32(P_AM_RING_OSC_REG0) & ~(0x3 << freq_sel_bit_lsb)) | (i << freq_sel_bit_lsb) );

        // Eanble the HS_HVT ring oscillator
        aml_write_reg32(P_AM_RING_OSC_REG0, aml_read_reg32(P_AM_RING_OSC_REG0) | (1 << osc_en_bit) );
        udelay(2);
        //freq_msr[i] = clk_util_clk_msr( osc_msr_mux_sel);
        freq_msr[i] = clk_util_clk_msr_pow( osc_msr_mux_sel, 10); //
        aml_write_reg32(P_ASSIST_AMR_SCRATCH0, freq_msr[i] );
        // Should not be zero
        if( freq_msr[i] == 0 ) {
            //stimulus_finish_fail(8);
	    pr_info("stimulus_finish_fail(8)\n");
        }
        // As we change the ring oscillator from slowest to fastest, the count values should increase by at least 10%
        pr_info("Measured count: %ld [dec]\n",freq_msr[i]);
	#if 0
        if( i > 0 ) {
            if( freq_msr[i] < (freq_msr[i-1]*1.1) ) {
                //stimulus_finish_fail(9);
                pr_info("stimulus_finish_fail(9)\n");
            }
        }
	#endif
    }

    // disable the ring oscillator
    aml_write_reg32(P_AM_RING_OSC_REG0, aml_read_reg32(P_AM_RING_OSC_REG0) & ~(1 << osc_en_bit) );
}

void test_ring_oscillator_entry(void)
{
	// ---------------------------------
	// Everything else domain
	// ---------------------------------
	pr_info("9T EE domain HVT Ring oscillator: %d\n\n",0);
	//	  .enable     ( am_ring_osc_cntl[1]	  ),  // 1 = enable
	//	  .freq_sel   ( am_ring_osc_cntl[3:2]	  ),  // 00 = low, 11 = high
	test_ring_oscillator(	2,	// unsigned long   freq_sel_bit_lsb,
				0,	// unsigned long   osc_en_bit,
				0 );	// unsigned long   osc_msr_mux_sel,

	pr_info("9T EE domain LVT Ring oscillator: %d\n\n",1);
	//	  .enable     ( am_ring_osc_cntl[0]	  ),  // 1 = enable
	//	  .freq_sel   ( am_ring_osc_cntl[5:4]	  ),  // 00 = low, 11 = high
	test_ring_oscillator(	4,	// unsigned long   freq_sel_bit_lsb,
				1,	// unsigned long   osc_en_bit,
				1 );	// unsigned long   osc_msr_mux_sel,

	// ---------------------------------
	// Check A9 Domain
	// ---------------------------------
	pr_info("12T A9 HVT Ring oscillator: %d\n\n",2);
	// .am_ring_osc_en_a9	       ( am_ring_osc_cntl[5:4]	   ),
	// .am_ring_freq_sel_a9        ( am_ring_osc_cntl[9:6]	   ),
	//				      am_ring_osc_out_a9[1],	  // [47]
	test_ring_oscillator(	8,	// unsigned long   freq_sel_bit_lsb,
				6,	// unsigned long   osc_en_bit,
				46 );	// unsigned long   osc_msr_mux_sel,

	pr_info("12T A9 LVT Ring oscillator: %d\n\n",3);
	// .am_ring_osc_en_a9	       ( am_ring_osc_cntl[5:4]	   ),
	// .am_ring_freq_sel_a9        ( am_ring_osc_cntl[9:6]	   ),
	//				      am_ring_osc_out_a9[0],	  // [46]
	test_ring_oscillator(  10,	// unsigned long   freq_sel_bit_lsb,
				7,	// unsigned long   osc_en_bit,
				47 );	// unsigned long   osc_msr_mux_sel,

	// ---------------------------------
	// Check Mali Domain
	// ---------------------------------
	//				       am_ring_osc_out_mali[1],    // [49]
	//				       am_ring_osc_out_mali[0],    // [48]
	//    .am_ring_osc_cntl_mali	  ( am_ring_osc_cntl[17:12]   ),
	pr_info("9T Mali HVT Ring oscillator: %d\n\n",4);
	//	   .enable	   ( am_ring_osc_cntl[0]   ),  // 1 = enable
	//	   .freq_sel	   ( am_ring_osc_cntl[3:2] ),  // 00 = low, 11 = high
	test_ring_oscillator(  14,	// unsigned long   freq_sel_bit_lsb,
			       12,	// unsigned long   osc_en_bit,
				48 );	// unsigned long   osc_msr_mux_sel,

	pr_info("9T Mali LVT Ring oscillator: %d\n\n",5);
	//	   .enable	   ( am_ring_osc_cntl[1]   ),  // 1 = enable
	//	   .freq_sel	   ( am_ring_osc_cntl[5:4] ),  // 00 = low, 11 = high
	test_ring_oscillator(  16,	// unsigned long   freq_sel_bit_lsb,
			       13,	// unsigned long   osc_en_bit,
				49 );	// unsigned long   osc_msr_mux_sel,
}

static ssize_t osc_show(struct class *cls,
			struct class_attribute *attr,
			char *buf)
{
	pr_info("Usage:");
	pr_info("	echo test all > /sys/class/regsiter/osc \n");
	//pr_info("	echo test <nr> > /sys/class/regsiter/osc \n");
	return 0;
}

static ssize_t osc_store(struct class *cls,
			 struct class_attribute *attr,
			 const char *buffer, size_t count)
{
	int n = 0;
	char *buf_orig, *ps, *token;
	char *parm[2];

	buf_orig = kstrdup(buffer, GFP_KERNEL);

	ps = buf_orig;
	while (1) {
		token = strsep(&ps, " \n");
		if (token == NULL)
			break;
		if (*token == '\0')
			continue;
		parm[n++] = token;
	}

	if (!strncmp(parm[0], "test", 4)) {
		if (!strncmp(parm[1], "all", 3)) {
			test_ring_oscillator_entry();
		}
	}
	return count;
}

static CLASS_ATTR(osc, S_IWUSR | S_IRUGO, osc_show, osc_store);


static int regd_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int regd_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}


static long regd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct reg_info reg;
	struct bit_info bit;
	unsigned int ret = 0;;
	memset(&reg, 0, sizeof(struct reg_info));
	memset(&bit, 0, sizeof(struct bit_info));

	switch (cmd) {
	/* reg ioctl */
	case IOC_RD_CBUS_REG:
		if (copy_from_user(&reg, argp, sizeof(struct reg_info)))
			return -EINVAL;
		reg.val = reg_read(CBUS_REG_ADDR(reg.addr));
		if (copy_to_user(argp, &reg, sizeof(struct reg_info)))
			return -EINVAL;
		break;
	case IOC_WR_CUBS_REG:
		if (copy_from_user(&reg, argp, sizeof(struct reg_info)))
			return -EINVAL;
		reg_write(CBUS_REG_ADDR(reg.addr), reg.val);
		break;
	case IOC_RD_APB_REG:
		if (copy_from_user(&reg, argp, sizeof(struct reg_info)))
			return -EINVAL;
		reg.val = reg_read(APB_REG_ADDR(reg.addr));
		if (copy_to_user(argp, &reg, sizeof(struct reg_info)))
			return -EINVAL;
		break;
	case IOC_WR_APB_REG:
		if (copy_from_user(&reg, argp, sizeof(struct reg_info)))
			return -EINVAL;
		reg_write(APB_REG_ADDR(reg.addr), reg.val);
		break;
	case IOC_RD_AAPB_REG:
		if (copy_from_user(&reg, argp, sizeof(struct reg_info)))
			return -EINVAL;
		reg.addr = reg.addr << 2;
		reg.val = reg_read(APB_REG_ADDR(reg.addr));
		if (copy_to_user(argp, &reg, sizeof(struct reg_info)))
			return -EINVAL;
		break;
	case IOC_WR_AAPB_REG:
		if (copy_from_user(&reg, argp, sizeof(struct reg_info)))
			return -EINVAL;
		reg.addr = reg.addr << 2;
		reg_write(APB_REG_ADDR(reg.addr), reg.val);
		break;
	case IOC_RD_AXI_REG:
		if (copy_from_user(&reg, argp, sizeof(struct reg_info)))
			return -EINVAL;
		reg.val = reg_read(AXI_REG_ADDR(reg.addr));
		if (copy_to_user(argp, &reg, sizeof(struct reg_info)))
			return -EINVAL;
		break;
	case IOC_WR_AXI_REG:
		if (copy_from_user(&reg, argp, sizeof(struct reg_info)))
			return -EINVAL;
		reg_write(AXI_REG_ADDR(reg.addr), reg.val);
		break;
	case IOC_RD_AHB_REG:
		if (copy_from_user(&reg, argp, sizeof(struct reg_info)))
			return -EINVAL;
		reg.val = reg_read(AHB_REG_ADDR(reg.addr));
		if (copy_to_user(argp, &reg, sizeof(struct reg_info)))
			return -EINVAL;
		break;
	case IOC_WR_AHB_REG:
		if (copy_from_user(&reg, argp, sizeof(struct reg_info)))
			return -EINVAL;
		reg_write(AHB_REG_ADDR(reg.addr), reg.val);
		break;
		/* bit ioctl */
	case IOC_RD_CBUS_BIT:
		if (copy_from_user(&bit, argp, sizeof(struct bit_info)))
			return -EINVAL;
		bit.val = bits_read(CBUS_REG_ADDR(bit.addr),bit.start, bit.len);
		if (copy_to_user(argp, &bit, sizeof(struct bit_info)))
			return -EINVAL;
		break;
	case IOC_WR_CUBS_BIT:
		if (copy_from_user(&bit, argp, sizeof(struct bit_info)))
			return -EINVAL;
		bits_write(CBUS_REG_ADDR(bit.addr), bit.val, bit.start, bit.len);
		break;
	case IOC_RD_APB_BIT:
		if (copy_from_user(&bit, argp, sizeof(struct bit_info)))
			return -EINVAL;
		bit.val = bits_read(APB_REG_ADDR(bit.addr), bit.start, bit.len);
		if (copy_to_user(argp, &bit, sizeof(struct bit_info)))
			return -EINVAL;
		break;
	case IOC_WR_APB_BIT:
		if (copy_from_user(&bit, argp, sizeof(struct bit_info)))
			return -EINVAL;
		bits_write(APB_REG_ADDR(bit.addr), bit.val,bit.start, bit.len);
		break;
	case IOC_RD_AXI_BIT:
		if (copy_from_user(&bit, argp, sizeof(struct bit_info)))
			return -EINVAL;
		bit.val = bits_read(AXI_REG_ADDR(bit.addr),bit.start, bit.len);
		if (copy_to_user(argp, &bit, sizeof(struct bit_info)))
			return -EINVAL;
		break;
	case IOC_WR_AXI_BIT:
		if (copy_from_user(&bit, argp, sizeof(struct bit_info)))
			return -EINVAL;
		bits_write(AXI_REG_ADDR(bit.addr), bit.val,bit.start, bit.len);
		break;
	case IOC_RD_AHB_BIT:
		if (copy_from_user(&bit, argp, sizeof(struct bit_info)))
			return -EINVAL;
		bit.val = bits_read(AHB_REG_ADDR(bit.addr), bit.start, bit.len);
		if (copy_to_user(argp, &bit, sizeof(struct bit_info)))
			return -EINVAL;
		break;
	case IOC_WR_AHB_BIT:
		if (copy_from_user(&bit, argp, sizeof(struct bit_info)))
			return -EINVAL;
		bits_write(AHB_REG_ADDR(bit.addr), bit.val, bit.start, bit.len);
		break;

	case IOC_WR_SGR_GAMMA:
	{
		int gamma_val = 0;
		unsigned int gammaR[256] = {0};
		char parm[10];
		if(copy_from_user(&gamma_val, argp, sizeof(int))){
			ret = EFAULT;
			break;
		}
		printk("gamma_val = %x\n",gamma_val);
		sprintf(parm, "%x", gamma_val);
		sscanf(parm, "%x", gammaR);
		printk("sgr:%s --> %d\n",parm, gammaR[0]);
		if(gammaR[0] > 598){
			gammaR[0] = 598;
		}
		set_lcd_gamma_table((u16 *)gammaR, H_SEL_R);
		break;
	}

	case IOC_WR_SGG_GAMMA:
	{
		int gamma_val = 0;
		unsigned int gammaR[256] = {0};
		char parm[10];
		if(copy_from_user(&gamma_val, argp, sizeof(int))){
			ret = EFAULT;
		break;
		}
		sprintf(parm, "%x", (int)gamma_val);
		sscanf(parm, "%x", gammaR);
		printk("sgg:%s --> %d\n",parm, gammaR[0]);
		if(gammaR[0] > 598){
			gammaR[0] = 598;
		}
		set_lcd_gamma_table((u16 *)gammaR, H_SEL_G);
		break;
	}

	case IOC_WR_SGB_GAMMA:
	{
		int gamma_val = 0;
		unsigned int gammaR[256] = {0};
		char parm[10];
		if(copy_from_user(&gamma_val, argp, sizeof(int))){
			ret = EFAULT;
			break;
		}
		sprintf(parm, "%x", (int)gamma_val);
		sscanf(parm, "%x", gammaR);
		printk("sgb:%s --> %d\n",parm, gammaR[0]);
		if(gammaR[0] > 598){
			gammaR[0] = 598;
		}
		set_lcd_gamma_table((u16 *)gammaR, H_SEL_B);
		break;
	}

	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

static struct file_operations regd_fops = {
	.owner		= THIS_MODULE,
	.open		= regd_open,
	.release	= regd_release,
	.unlocked_ioctl	= regd_ioctl,
};

static int __init regd_init(void)
{
	int ret = 0;

	ret = alloc_chrdev_region(&regd_devt, 0, 1, DREG_DEV_NAME);
	if (ret < 0) {
		pr_err("%s: failed to allocate major number\n", __func__);
		goto fail_alloc_cdev_region;
	}

	regd_clsp = class_create(THIS_MODULE, DREG_CLS_NAME);
	if (IS_ERR(regd_clsp)) {
		ret = PTR_ERR(regd_clsp);
		pr_err("%s: failed to create class\n", __func__);
		goto fail_class_create;
	}

	cdev_init(&regd_cdev, &regd_fops);
	regd_cdev.owner = THIS_MODULE;
	cdev_add(&regd_cdev, regd_devt, 1);

	regd_devp = device_create(regd_clsp, NULL, regd_devt, NULL, DREG_DEV_NAME);
	if (IS_ERR(regd_devp)) {
		ret = PTR_ERR(regd_devp);
		goto fail_create_device;
	}

	ret =class_create_file(regd_clsp, &class_attr_reg);
	ret =class_create_file(regd_clsp, &class_attr_bit);
	ret =class_create_file(regd_clsp, &class_attr_gamma);
	ret =class_create_file(regd_clsp, &class_attr_cm2);
	ret =class_create_file(regd_clsp, &class_attr_osc);

	return 0;

fail_create_device:
	cdev_del(&regd_cdev);
	class_destroy(regd_clsp);
fail_class_create:
	unregister_chrdev_region(regd_devt, 1);
fail_alloc_cdev_region:
	return ret;
}

static void __exit regd_exit(void)
{
	device_destroy(regd_clsp, regd_devt);
	cdev_del(&regd_cdev);
	class_destroy(regd_clsp);
	unregister_chrdev_region(regd_devt, 1);
}

module_init(regd_init);
module_exit(regd_exit);

MODULE_DESCRIPTION("Amlogic Register Debug Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zhihua Xie <zhihua.xie@amlogic.com>");


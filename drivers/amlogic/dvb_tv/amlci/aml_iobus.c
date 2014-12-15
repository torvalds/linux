
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <plat/io.h>
#include <mach/register.h>
#include <mach/pinmux.h>

#include "aml_iobus.h"

static int aml_iobus_debug=0;
module_param_named(iobus_debug, aml_iobus_debug, int, 0644);
MODULE_PARM_DESC(iobus_debug, "enable verbose debug messages");

static spinlock_t iobus_lock;

#define pr_dbg(fmt, args...) do{if (aml_iobus_debug) printk("IOBUS: " fmt, ## args);}while(0)
#define pr_error(fmt, args...) printk(KERN_ERR "IOBUS: " fmt, ## args)

#define event_length_bit  24
#define bus_1st_sel_1_bit 22  // 00-gpio, 01-reserved, 10-addr, 11-data
#define bus_2nd_sel_1_bit 20
#define bus_1st_sel_0_bit 18
#define bus_2nd_sel_0_bit 16

#define clock_divide_ext_bit   24
#define s_bus_start_bit        20
#define no_sclk_on_pin_bit     19
#define invert_sclk_in_bit     18
#define sdata_send_busy_bit    17
#define one_sdata_received_bit 16
#define sdata_parity_bit       15
#define sdata_send_type_bit    14
#define sdata_receive_type_bit 13
#define invert_request_out_bit 12
#define request_out_sel_bit     8
#define stop_request_count_bit  0

#define bus_width_1_bit 28
#define bus_start_pin_1_bit 24
#define bus_sel_chang_point_1_bit 16
#define bus_width_0_bit 12
#define bus_start_pin_0_bit 8
#define bus_sel_chang_point_0_bit 0

#define clock_divide_bit      24
#define clock_output_sel_bit  20
#define inc_event_addr_bit       19
#define async_fifo_endian_bit    18
#define send_to_async_fifo_bit   17
#define data_in_serial_lsb_bit   16
#define invert_no_wait_condition_2_0_bit   15
#define invert_no_wait_condition_2_1_bit   14
#define invert_no_wait_condition_2_2_bit   13
#define invert_data_bus_bit   12
#define invert_clock_in_bit   11
#define event_wait_clk_en_bit 10
#define data_in_serial_bit     9
#define invert_data_in_clk_bit 8
#define data_in_begin_bit      4
#define data_in_clk_sel_bit    0

#define     no_wait_condition_0_bit  28
#define     no_wait_condition_1_bit  24
#define     no_wait_condition_2_bit  20
#define     irq_input_sel_bit        16
#define     interrupt_status_bit     13
#define     enable_sdata_irq         12
#define     invert_irq_0_bit         11
#define     invert_irq_1_bit         10
#define     enable_transfer_end_irq   9
#define     enable_second_ext_irq_bit 8
#define     no_wait_condition_check_point_bit 0

#define SETnXcfg()	\
	({ \
		aml_set_reg32_bits(P_PREG_PAD_GPIO0_EN_N, 0, 28, 2);/*GPIOA28~29 OUT*/ \
		aml_set_reg32_bits(P_PREG_PAD_GPIO1_EN_N, 1, 12, 1);/*GPIOB12 IN*/ \
	})
#define SETAcfg()		aml_set_reg32_bits(P_PREG_PAD_GPIO0_EN_N, 0, 0, 12) /*GPIOA0~11 OUT*/

#define SETnCE(v)		aml_set_reg32_bits(P_PREG_PAD_GPIO0_O, (v), 29, 1)
#define SETnREG(v)	aml_set_reg32_bits(P_PREG_PAD_GPIO0_O, (v), 28, 1)
#define GETIREQ()		aml_get_reg32_bits(P_PREG_PAD_GPIO1_I, 12, 1)

#define EN(c)		SETn##c(0)
#define DIS(c)	SETn##c(1)

#define RDDELAY(n) msleep(n)
#define WRDELAY(n) msleep(n)


// For Attr Write Signal
#define CIPLUS_WR_OUTOUT_CF0            0x000004f5      //WR  0x0000040f
#define CIPLUS_WR_OUTOUT_CF1            0x0001ffff      //RD
#define CIPLUS_WR_OUTOUT_CF2            0x0002ffff
#define CIPLUS_WR_OUTOUT_CF3            0x0003ffff
// For Attr Read Signal
#define CIPLUS_RD_OUTOUT_CF0            0x0000ffff
#define CIPLUS_RD_OUTOUT_CF1            0x000104f5
#define CIPLUS_RD_OUTOUT_CF2            0x0002ffff
#define CIPLUS_RD_OUTOUT_CF3            0x0003ffff
// For Attr Write Bus
#define CIPLUS_WR_BUS_CONFIG ( \
                                (3  << bus_width_1_bit) \
                               |(13 << bus_start_pin_1_bit ) \
                               |(3  << bus_sel_chang_point_1_bit ) \
                               |(8  << bus_width_0_bit) \
                               |(5  << bus_start_pin_0_bit ) \
                               |(3  << bus_sel_chang_point_0_bit ) \
                             )
// For Attr Read Bus
#define CIPLUS_RD_BUS_CONFIG ( \
                                (3  << bus_width_1_bit) \
                               |(13 << bus_start_pin_1_bit ) \
                               |(3  << bus_sel_chang_point_1_bit ) \
                               |(8  << bus_width_0_bit) \
                               |(5  << bus_start_pin_0_bit ) \
                               |(3  << bus_sel_chang_point_0_bit ) \
                             )
void ci_wr_config(void)
{
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_WR_OUTOUT_CF0);
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_WR_OUTOUT_CF1);
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_WR_OUTOUT_CF2);
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_WR_OUTOUT_CF3);
}
void ci_rd_config(void)
{
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_RD_OUTOUT_CF0);
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_RD_OUTOUT_CF1);
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_RD_OUTOUT_CF2);
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_RD_OUTOUT_CF3);
}

// For IO Write Signal
#define CIPLUS_IOWR_OUTOUT_CF0            0x0000ffff
#define CIPLUS_IOWR_OUTOUT_CF1            0x0001ffff
#define CIPLUS_IOWR_OUTOUT_CF2            0x0002ffff       // IORD
#define CIPLUS_IOWR_OUTOUT_CF3            0x000304f5       // IOWR
// For IO Read Signal
#define CIPLUS_IORD_OUTOUT_CF0            0x0000ffff
#define CIPLUS_IORD_OUTOUT_CF1            0x0001ffff
#define CIPLUS_IORD_OUTOUT_CF2            0x000204f5
#define CIPLUS_IORD_OUTOUT_CF3            0x0003ffff
// For IO Write Bus
#define CIPLUS_IOWR_BUS_CONFIG ( \
                                (2  << bus_width_1_bit) \
                               |(13 << bus_start_pin_1_bit ) \
                               |(3  << bus_sel_chang_point_1_bit ) \
                               |(8  << bus_width_0_bit) \
                               |(5  << bus_start_pin_0_bit ) \
                               |(3  << bus_sel_chang_point_0_bit ) \
                             )
// For IO Read Bus
#define CIPLUS_IORD_BUS_CONFIG ( \
                                (2  << bus_width_1_bit) \
                               |(13 << bus_start_pin_1_bit ) \
                               |(3  << bus_sel_chang_point_1_bit ) \
                               |(8  << bus_width_0_bit) \
                               |(5  << bus_start_pin_0_bit ) \
                               |(3  << bus_sel_chang_point_0_bit ) \
                             )
void ci_iowr_config(void)
{
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_IOWR_OUTOUT_CF0);
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_IOWR_OUTOUT_CF1);
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_IOWR_OUTOUT_CF2);
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_IOWR_OUTOUT_CF3);
}
void ci_iord_config(void)
{
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_IORD_OUTOUT_CF0);
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_IORD_OUTOUT_CF1);
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_IORD_OUTOUT_CF2);
	aml_write_reg32(P_STREAM_OUTPUT_CONFIG, CIPLUS_IORD_OUTOUT_CF3);
}

#define CIPLUS_W_EVENT_INFO ( \
                                (254     << event_length_bit ) \
                               |(2      << bus_1st_sel_1_bit) \
                               |(2      << bus_2nd_sel_1_bit) \
                               |(1      << bus_1st_sel_0_bit) \
                               |(3      << bus_2nd_sel_0_bit) \
                               |(0xffff ) \
                             )
#define CIPLUS_R_EVENT_INFO ( \
                                (245     << event_length_bit ) \
                               |(2      << bus_1st_sel_1_bit) \
                               |(2      << bus_2nd_sel_1_bit) \
                               |(1      << bus_1st_sel_0_bit) \
                               |(1      << bus_2nd_sel_0_bit) \
                               |(0xffff ) \
                             )
#define CIPLUS_DATA_IN_OE ( \
                                (1  << data_in_clk_sel_bit) \
                               |(5   <<data_in_begin_bit) \
                             )
#define CIPLUS_DATA_IN_IORD ( \
                                (2  << data_in_clk_sel_bit) \
                               |(5   <<data_in_begin_bit) \
                             )

unsigned int aml_iobus_attr_read(unsigned int addr)
{
	unsigned int event_ctrl;
	unsigned int data;
	unsigned char val;
	unsigned int addrl = addr&0x7;
	unsigned char addrh = addr >> 3;

	aml_set_reg32_bits(P_PREG_PAD_GPIO0_O, (addrh&0xfff), 0, 12);

	ci_rd_config();
	// Config bus, data/addr
	aml_write_reg32(P_STREAM_BUS_CONFIG, CIPLUS_RD_BUS_CONFIG);
	// Config event info
	aml_write_reg32(P_STREAM_EVENT_INFO, CIPLUS_R_EVENT_INFO);
	// Config data_in
	aml_write_reg32(P_STREAM_DATA_IN_CONFIG, CIPLUS_DATA_IN_OE);

	// Config event ctrl
	event_ctrl =  (addrl<<24)      // address
			|(0<<16)         // data
			|(0<<1)          // repeat 0 times, total 1 times
			|1;              // start event
	aml_write_reg32(P_STREAM_EVENT_CTL, event_ctrl);

	// Test Rd Finish, and Close Stream Interface
	while(1){
		if((aml_read_reg32(P_STREAM_EVENT_CTL) & 0x00000001) == 0) break;
	}

	data = aml_read_reg32(P_STREAM_EVENT_CTL);
	val  = (data>>16);
	aml_write_reg32(P_STREAM_EVENT_INFO, 0x0000ffff);
//	printk("Read : ATTR[0x%x] = 0x%x\n", addr, val);
	return val;
}
EXPORT_SYMBOL(aml_iobus_attr_read);

int aml_iobus_attr_write(unsigned int addr, unsigned char val)
{
	unsigned int event_ctrl;
	unsigned int addrl = addr&0x7;
	unsigned char addrh = addr >> 3;

	aml_set_reg32_bits(P_PREG_PAD_GPIO0_O, (addrh&0xfff), 0, 12);

	ci_wr_config();
	// Config bus, data/addr
	aml_write_reg32(P_STREAM_BUS_CONFIG, CIPLUS_WR_BUS_CONFIG);
	// Config event info
	aml_write_reg32(P_STREAM_EVENT_INFO, CIPLUS_W_EVENT_INFO);

	// Config event ctrl
	event_ctrl =  (addrl<<24)      // address
			|((val)<<16)    // data
			|(0<<1)          // repeat 0 times, total 1 times
			|1;              // start event
	aml_write_reg32(P_STREAM_EVENT_CTL, event_ctrl);

	// Test Wr Finish, and Close STREAM Interface
	while(1){
		if((aml_read_reg32(P_STREAM_EVENT_CTL) & 0x00000001) == 0) break;
	}

	aml_write_reg32(P_STREAM_EVENT_INFO, 0x0000ffff);
	printk("Write : ATTR[0x%x] = 0x%x\n", addr, val);
	return 0;
}
EXPORT_SYMBOL(aml_iobus_attr_write);

unsigned int aml_iobus_io_read(unsigned char addr)
{
	unsigned int event_ctrl;
	unsigned int data;
	unsigned char val;

	ci_iord_config();
	// Config bus, data/addr
	aml_write_reg32(P_STREAM_BUS_CONFIG, CIPLUS_IORD_BUS_CONFIG);
	// Config event info
	aml_write_reg32(P_STREAM_EVENT_INFO, CIPLUS_R_EVENT_INFO);
	// Config data_in
	aml_write_reg32(P_STREAM_DATA_IN_CONFIG, CIPLUS_DATA_IN_IORD);

	// Config event ctrl
	event_ctrl =  (addr<<24)      // address
			|(0<<16)         // data
			|(0<<1)          // repeat 0 times, total 1 times
			|1;              // start event
	aml_write_reg32(P_STREAM_EVENT_CTL, event_ctrl);

	// Test Rd Finish, and Close Stream Interface
	while(1){
		if((aml_read_reg32(P_STREAM_EVENT_CTL) & 0x00000001) == 0) break;
	}

	data = aml_read_reg32(P_STREAM_EVENT_CTL);
	val  = (data>>16);
	aml_write_reg32(P_STREAM_EVENT_INFO, 0x0000ffff);
//	printk("Read : IO[0x%x] = 0x%x\n", addr, val);
	return val;
}
EXPORT_SYMBOL(aml_iobus_io_read);

int aml_iobus_io_write(unsigned char addr, unsigned char val)
{
	unsigned int event_ctrl;

	ci_iowr_config();
	// Config bus, data/addr
	aml_write_reg32(P_STREAM_BUS_CONFIG, CIPLUS_IOWR_BUS_CONFIG);
	// Config event info
	aml_write_reg32(P_STREAM_EVENT_INFO, CIPLUS_W_EVENT_INFO);

	// Config event ctrl
	event_ctrl =  (addr<<24)      // address
			|((val)<<16)    // data
			|(0<<1)          // repeat 0 times, total 1 times
			|1;              // start event
	aml_write_reg32(P_STREAM_EVENT_CTL, event_ctrl);

	// Test Wr Finish, and Close STREAM Interface
	while(1){
		if((aml_read_reg32(P_STREAM_EVENT_CTL) & 0x00000001) == 0) break;
	}

	aml_write_reg32(P_STREAM_EVENT_INFO, 0x0000ffff);
//	printk("Write : IO[0x%x] = 0x%x\n", addr, val);
	return 0;
}
EXPORT_SYMBOL(aml_iobus_io_write);


static int io_set_pinmux(void)
{
	//gpioA00-A11 gpioA28-A29:
		//reg0[6-10],reg3[0-5],reg4[18-21],reg7[16-17],reg6[20-23],reg8[11]
	//streamif00-15:
		//reg7[0-15]
	static pinmux_item_t bus_gpioA_pins[] = {
	    {
	        .reg = PINMUX_REG(0),
	        .clrmask = 0x1f << 6
	    },
	    {
	        .reg = PINMUX_REG(3),
	        .clrmask = 0x3f
	    },
	    {
	        .reg = PINMUX_REG(4),
	        .clrmask = 0xf << 18
	    },
	    {
	        .reg = PINMUX_REG(7),
	        .setmask = 0xffff
	    },
	    {
	        .reg = PINMUX_REG(7),
	        .clrmask = 0x3 << 16
	    },
	    {
	        .reg = PINMUX_REG(6),
	        .clrmask = 0xf << 20
	    },
	    {
	        .reg = PINMUX_REG(8),
	        .clrmask = 0x1 << 11
	    },
	    PINMUX_END_ITEM
	};
	static pinmux_set_t bus_gpioA_pinmux_set = {
	    .chip_select = NULL,
	    .pinmux = &bus_gpioA_pins[0]
	};
	pinmux_set(&bus_gpioA_pinmux_set);

	//gpioB12:
		//reg0[2],reg3[17],reg5[5]
	static pinmux_item_t bus_gpioB_pins[] = {
	    {
	        .reg = PINMUX_REG(0),
	        .clrmask = 0x1 << 2
	    },
	    {
	        .reg = PINMUX_REG(3),
	        .clrmask = 0x1 << 17
	    },
	    {
	        .reg = PINMUX_REG(5),
	        .clrmask = 0x1 << 5
	    },
	    PINMUX_END_ITEM
	};
	static pinmux_set_t bus_gpioB_pinmux_set = {
	    .chip_select = NULL,
	    .pinmux = &bus_gpioB_pins[0]
	};
	pinmux_set(&bus_gpioB_pinmux_set);

	return 0;
}

int aml_iobus_init(void)
{
	aml_write_reg32(P_STREAM_EVENT_INFO, 0xffff);
	io_set_pinmux();

	/*init gpio direction*/
	SETnXcfg();
	SETAcfg();

	/*init gpio states*/
	EN(REG);
	EN(CE);

	spin_lock_init(&iobus_lock);

	return 0;
}
EXPORT_SYMBOL(aml_iobus_init);

int aml_iobus_exit(void)
{
	return 0;
}
EXPORT_SYMBOL(aml_iobus_exit);



#include <linux/device.h>
#include <linux/slab.h>

static ssize_t aml_iobus_write_help(struct class *class, struct class_attribute *attr, char *buf)
{
	int ret;
	ret = sprintf(buf, "echo XXXX > %s\n\tXXXX - value in hex.\n", attr->attr.name);
	return ret;
}
static ssize_t aml_iobus_iow_help(struct class *class, struct class_attribute *attr, char *buf)
{
	int ret;
	ret = sprintf(buf, "echo (r|w)(i|a) addr data > %s\n\taddr, data - value in hex.\n", attr->attr.name);
	return ret;
}
static ssize_t aml_iobus_test_addr_write(struct class *class,struct class_attribute *attr,
                          const char *buf,
                          size_t size)
{
	unsigned int d = simple_strtol(buf,0,16);
	printk("AD:%x\n", d);
//	SETADDR(d);
	return size;
}
static ssize_t aml_iobus_test_data_write(struct class *class,struct class_attribute *attr,
                          const char *buf,
                          size_t size)
{
	unsigned int d = simple_strtol(buf,0,16);
	printk("DA:%x\n", d);
//	SETDATA(d);
	return size;
}
static ssize_t aml_iobus_test_io_write(struct class *class,struct class_attribute *attr,
                          const char *buf,
                          size_t size)
{
    int n = 0;
    char *buf_orig, *ps, *token;
    char *parm[3];
    unsigned int addr = 0, val = 0, retval = 0;

	buf_orig = kstrdup(buf, GFP_KERNEL);
	ps = buf_orig;
	while (1) {
		token = strsep(&ps, " \n");
		if (token == NULL)
			break;
		if (*token == '\0')
			continue;
        parm[n++] = token;
	}

    if ((n > 0) && (strlen(parm[0]) != 2))
    {
        pr_err("invalid command\n");
        kfree(buf_orig);
        return size;
    }

    if ((parm[0][0] == 'r'))
    {
        if (n != 2)
        {
            pr_err("read: invalid parameter\n");
            kfree(buf_orig);
            return size;
        }
        addr = simple_strtol(parm[1], NULL, 16);
        pr_err("%s 0x%x\n", parm[0], addr);
        switch (parm[0][1])
        {
            case 'i':
                retval = aml_iobus_io_read(addr);
                break;
            case 'a':
                retval = aml_iobus_attr_read(addr);
                break;
	     default:
		 	break;
        }
        pr_info("%s: 0x%x --> 0x%x\n", parm[0], addr, retval);
    }
    else if ((parm[0][0] == 'w'))
    {
        if (n != 3)
        {
            pr_err("write: invalid parameter\n");
            kfree(buf_orig);
            return size;
        }
        addr = simple_strtol(parm[1], NULL, 16);
        val  = simple_strtol(parm[2], NULL, 16);
        pr_err("%s 0x%x 0x%x", parm[0], addr, val);
        switch (parm[0][1])
        {
            case 'i':
	        retval = aml_iobus_io_write(addr, val);
                break;
            case 'a':
	        retval = aml_iobus_attr_write(addr, val);
                break;
	     default:
		 	break;
        }
        pr_info("%s: 0x%x <-- 0x%x\n", parm[0], addr, retval);
    }
    else
    {
        pr_err("invalid command\n");
    }

	kfree(buf_orig);
    return size;
}

static struct class_attribute aml_iobus_class_attrs[] = {
    __ATTR(ad,  S_IRUGO | S_IWUSR, aml_iobus_write_help, aml_iobus_test_addr_write),
    __ATTR(da,  S_IRUGO | S_IWUSR, aml_iobus_write_help, aml_iobus_test_data_write),
    __ATTR(io,  S_IRUGO | S_IWUSR, aml_iobus_iow_help, aml_iobus_test_io_write),
    __ATTR_NULL
};

static struct class aml_iobus_class = {
    .name = "aml_iobus_test",
    .class_attrs = aml_iobus_class_attrs,
};

static int __init aml_iobus_mod_init(void)
{
	printk("Amlogic IOBUS Init\n");
	class_register(&aml_iobus_class);
	return 0;
}

static void __exit aml_iobus_mod_exit(void)
{
	printk("Amlogic IOBUS Exit\n");
	class_unregister(&aml_iobus_class);
}

module_init(aml_iobus_mod_init);
module_exit(aml_iobus_mod_exit);

MODULE_LICENSE("GPL");



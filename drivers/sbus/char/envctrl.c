/* $Id: envctrl.c,v 1.25 2002/01/15 09:01:26 davem Exp $
 * envctrl.c: Temperature and Fan monitoring on Machines providing it.
 *
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 2000  Vinh Truong    (vinh.truong@eng.sun.com)
 * VT - The implementation is to support Sun Microelectronics (SME) platform
 *      environment monitoring.  SME platforms use pcf8584 as the i2c bus 
 *      controller to access pcf8591 (8-bit A/D and D/A converter) and 
 *      pcf8571 (256 x 8-bit static low-voltage RAM with I2C-bus interface).
 *      At board level, it follows SME Firmware I2C Specification. Reference:
 * 	http://www-eu2.semiconductors.com/pip/PCF8584P
 * 	http://www-eu2.semiconductors.com/pip/PCF8574AP
 * 	http://www-eu2.semiconductors.com/pip/PCF8591P
 *
 * EB - Added support for CP1500 Global Address and PS/Voltage monitoring.
 * 		Eric Brower <ebrower@usa.net>
 *
 * DB - Audit every copy_to_user in envctrl_read.
 *              Daniele Bellucci <bellucda@tiscali.it>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/kernel.h>

#include <asm/ebus.h>
#include <asm/uaccess.h>
#include <asm/envctrl.h>

#define __KERNEL_SYSCALLS__
static int errno;
#include <asm/unistd.h>

#define ENVCTRL_MINOR	162

#define PCF8584_ADDRESS	0x55

#define CONTROL_PIN	0x80
#define CONTROL_ES0	0x40
#define CONTROL_ES1	0x20
#define CONTROL_ES2	0x10
#define CONTROL_ENI	0x08
#define CONTROL_STA	0x04
#define CONTROL_STO	0x02
#define CONTROL_ACK	0x01

#define STATUS_PIN	0x80
#define STATUS_STS	0x20
#define STATUS_BER	0x10
#define STATUS_LRB	0x08
#define STATUS_AD0	0x08
#define STATUS_AAB	0x04
#define STATUS_LAB	0x02
#define STATUS_BB	0x01

/*
 * CLK Mode Register.
 */
#define BUS_CLK_90	0x00
#define BUS_CLK_45	0x01
#define BUS_CLK_11	0x02
#define BUS_CLK_1_5	0x03

#define CLK_3		0x00
#define CLK_4_43	0x10
#define CLK_6		0x14
#define CLK_8		0x18
#define CLK_12		0x1c

#define OBD_SEND_START	0xc5    /* value to generate I2c_bus START condition */
#define OBD_SEND_STOP 	0xc3    /* value to generate I2c_bus STOP condition */

/* Monitor type of i2c child device.
 * Firmware definitions.
 */
#define PCF8584_MAX_CHANNELS            8
#define PCF8584_GLOBALADDR_TYPE			6  /* global address monitor */
#define PCF8584_FANSTAT_TYPE            3  /* fan status monitor */
#define PCF8584_VOLTAGE_TYPE            2  /* voltage monitor    */
#define PCF8584_TEMP_TYPE	        	1  /* temperature monitor*/

/* Monitor type of i2c child device.
 * Driver definitions.
 */
#define ENVCTRL_NOMON				0
#define ENVCTRL_CPUTEMP_MON			1    /* cpu temperature monitor */
#define ENVCTRL_CPUVOLTAGE_MON	  	2    /* voltage monitor         */
#define ENVCTRL_FANSTAT_MON  		3    /* fan status monitor      */
#define ENVCTRL_ETHERTEMP_MON		4    /* ethernet temperarture */
					     /* monitor                     */
#define ENVCTRL_VOLTAGESTAT_MON	  	5    /* voltage status monitor  */
#define ENVCTRL_MTHRBDTEMP_MON		6    /* motherboard temperature */
#define ENVCTRL_SCSITEMP_MON		7    /* scsi temperarture */
#define ENVCTRL_GLOBALADDR_MON		8    /* global address */

/* Child device type.
 * Driver definitions.
 */
#define I2C_ADC				0    /* pcf8591 */
#define I2C_GPIO			1    /* pcf8571 */

/* Data read from child device may need to decode
 * through a data table and a scale.
 * Translation type as defined by firmware.
 */
#define ENVCTRL_TRANSLATE_NO		0
#define ENVCTRL_TRANSLATE_PARTIAL	1
#define ENVCTRL_TRANSLATE_COMBINED	2
#define ENVCTRL_TRANSLATE_FULL		3     /* table[data] */
#define ENVCTRL_TRANSLATE_SCALE		4     /* table[data]/scale */

/* Driver miscellaneous definitions. */
#define ENVCTRL_MAX_CPU			4
#define CHANNEL_DESC_SZ			256

/* Mask values for combined GlobalAddress/PowerStatus node */
#define ENVCTRL_GLOBALADDR_ADDR_MASK 	0x1F
#define ENVCTRL_GLOBALADDR_PSTAT_MASK	0x60

/* Node 0x70 ignored on CompactPCI CP1400/1500 platforms 
 * (see envctrl_init_i2c_child)
 */
#define ENVCTRL_CPCI_IGNORED_NODE		0x70

#define PCF8584_DATA	0x00
#define PCF8584_CSR	0x01

/* Each child device can be monitored by up to PCF8584_MAX_CHANNELS.
 * Property of a port or channel as defined by the firmware.
 */
struct pcf8584_channel {
        unsigned char chnl_no;
        unsigned char io_direction;
        unsigned char type;
        unsigned char last;
};

/* Each child device may have one or more tables of bytes to help decode
 * data. Table property as defined by the firmware.
 */ 
struct pcf8584_tblprop {
        unsigned int type;
        unsigned int scale;  
        unsigned int offset; /* offset from the beginning of the table */
        unsigned int size;
};

/* i2c child */
struct i2c_child_t {
	/* Either ADC or GPIO. */
	unsigned char i2ctype;
        unsigned long addr;    
        struct pcf8584_channel chnl_array[PCF8584_MAX_CHANNELS];

	/* Channel info. */ 
	unsigned int total_chnls;	/* Number of monitor channels. */
	unsigned char fan_mask;		/* Byte mask for fan status channels. */
	unsigned char voltage_mask;	/* Byte mask for voltage status channels. */
        struct pcf8584_tblprop tblprop_array[PCF8584_MAX_CHANNELS];

	/* Properties of all monitor channels. */
	unsigned int total_tbls;	/* Number of monitor tables. */
        char *tables;			/* Pointer to table(s). */
	char chnls_desc[CHANNEL_DESC_SZ]; /* Channel description. */
	char mon_type[PCF8584_MAX_CHANNELS];
};

static void __iomem *i2c;
static struct i2c_child_t i2c_childlist[ENVCTRL_MAX_CPU*2];
static unsigned char chnls_mask[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
static unsigned int warning_temperature = 0;
static unsigned int shutdown_temperature = 0;
static char read_cpu;

/* Forward declarations. */
static struct i2c_child_t *envctrl_get_i2c_child(unsigned char);

/* Function Description: Test the PIN bit (Pending Interrupt Not) 
 * 			 to test when serial transmission is completed .
 * Return : None.
 */
static void envtrl_i2c_test_pin(void)
{
	int limit = 1000000;

	while (--limit > 0) {
		if (!(readb(i2c + PCF8584_CSR) & STATUS_PIN)) 
			break;
		udelay(1);
	} 

	if (limit <= 0)
		printk(KERN_INFO "envctrl: Pin status will not clear.\n");
}

/* Function Description: Test busy bit.
 * Return : None.
 */
static void envctrl_i2c_test_bb(void)
{
	int limit = 1000000;

	while (--limit > 0) {
		/* Busy bit 0 means busy. */
		if (readb(i2c + PCF8584_CSR) & STATUS_BB)
			break;
		udelay(1);
	} 

	if (limit <= 0)
		printk(KERN_INFO "envctrl: Busy bit will not clear.\n");
}

/* Function Description: Send the address for a read access.
 * Return : 0 if not acknowledged, otherwise acknowledged.
 */
static int envctrl_i2c_read_addr(unsigned char addr)
{
	envctrl_i2c_test_bb();

	/* Load address. */
	writeb(addr + 1, i2c + PCF8584_DATA);

	envctrl_i2c_test_bb();

	writeb(OBD_SEND_START, i2c + PCF8584_CSR);

	/* Wait for PIN. */
	envtrl_i2c_test_pin();

	/* CSR 0 means acknowledged. */
	if (!(readb(i2c + PCF8584_CSR) & STATUS_LRB)) {
		return readb(i2c + PCF8584_DATA);
	} else {
		writeb(OBD_SEND_STOP, i2c + PCF8584_CSR);
		return 0;
	}
}

/* Function Description: Send the address for write mode.  
 * Return : None.
 */
static void envctrl_i2c_write_addr(unsigned char addr)
{
	envctrl_i2c_test_bb();
	writeb(addr, i2c + PCF8584_DATA);

	/* Generate Start condition. */
	writeb(OBD_SEND_START, i2c + PCF8584_CSR);
}

/* Function Description: Read 1 byte of data from addr 
 *			 set by envctrl_i2c_read_addr() 
 * Return : Data from address set by envctrl_i2c_read_addr().
 */
static unsigned char envctrl_i2c_read_data(void)
{
	envtrl_i2c_test_pin();
	writeb(CONTROL_ES0, i2c + PCF8584_CSR);  /* Send neg ack. */
	return readb(i2c + PCF8584_DATA);
}

/* Function Description: Instruct the device which port to read data from.  
 * Return : None.
 */
static void envctrl_i2c_write_data(unsigned char port)
{
	envtrl_i2c_test_pin();
	writeb(port, i2c + PCF8584_DATA);
}

/* Function Description: Generate Stop condition after last byte is sent.
 * Return : None.
 */
static void envctrl_i2c_stop(void)
{
	envtrl_i2c_test_pin();
	writeb(OBD_SEND_STOP, i2c + PCF8584_CSR);
}

/* Function Description: Read adc device.
 * Return : Data at address and port.
 */
static unsigned char envctrl_i2c_read_8591(unsigned char addr, unsigned char port)
{
	/* Send address. */
	envctrl_i2c_write_addr(addr);

	/* Setup port to read. */
	envctrl_i2c_write_data(port);
	envctrl_i2c_stop();

	/* Read port. */
	envctrl_i2c_read_addr(addr);

	/* Do a single byte read and send stop. */
	envctrl_i2c_read_data();
	envctrl_i2c_stop();

	return readb(i2c + PCF8584_DATA);
}

/* Function Description: Read gpio device.
 * Return : Data at address.
 */
static unsigned char envctrl_i2c_read_8574(unsigned char addr)
{
	unsigned char rd;

	envctrl_i2c_read_addr(addr);

	/* Do a single byte read and send stop. */
	rd = envctrl_i2c_read_data();
	envctrl_i2c_stop();
	return rd;
}

/* Function Description: Decode data read from an adc device using firmware
 *                       table.
 * Return: Number of read bytes. Data is stored in bufdata in ascii format.
 */
static int envctrl_i2c_data_translate(unsigned char data, int translate_type,
				      int scale, char *tbl, char *bufdata)
{
	int len = 0;

	switch (translate_type) {
	case ENVCTRL_TRANSLATE_NO:
		/* No decode necessary. */
		len = 1;
		bufdata[0] = data;
		break;

	case ENVCTRL_TRANSLATE_FULL:
		/* Decode this way: data = table[data]. */
		len = 1;
		bufdata[0] = tbl[data];
		break;

	case ENVCTRL_TRANSLATE_SCALE:
		/* Decode this way: data = table[data]/scale */
		sprintf(bufdata,"%d ", (tbl[data] * 10) / (scale));
		len = strlen(bufdata);
		bufdata[len - 1] = bufdata[len - 2];
		bufdata[len - 2] = '.';
		break;

	default:
		break;
	};

	return len;
}

/* Function Description: Read cpu-related data such as cpu temperature, voltage.
 * Return: Number of read bytes. Data is stored in bufdata in ascii format.
 */
static int envctrl_read_cpu_info(int cpu, struct i2c_child_t *pchild,
				 char mon_type, unsigned char *bufdata)
{
	unsigned char data;
	int i;
	char *tbl, j = -1;

	/* Find the right monitor type and channel. */
	for (i = 0; i < PCF8584_MAX_CHANNELS; i++) {
		if (pchild->mon_type[i] == mon_type) {
			if (++j == cpu) {
				break;
			}
		}
	}

	if (j != cpu)
		return 0;

        /* Read data from address and port. */
	data = envctrl_i2c_read_8591((unsigned char)pchild->addr,
				     (unsigned char)pchild->chnl_array[i].chnl_no);

	/* Find decoding table. */
	tbl = pchild->tables + pchild->tblprop_array[i].offset;

	return envctrl_i2c_data_translate(data, pchild->tblprop_array[i].type,
					  pchild->tblprop_array[i].scale,
					  tbl, bufdata);
}

/* Function Description: Read noncpu-related data such as motherboard 
 *                       temperature.
 * Return: Number of read bytes. Data is stored in bufdata in ascii format.
 */
static int envctrl_read_noncpu_info(struct i2c_child_t *pchild,
				    char mon_type, unsigned char *bufdata)
{
	unsigned char data;
	int i;
	char *tbl = NULL;

	for (i = 0; i < PCF8584_MAX_CHANNELS; i++) {
		if (pchild->mon_type[i] == mon_type)
			break;
	}

	if (i >= PCF8584_MAX_CHANNELS)
		return 0;

        /* Read data from address and port. */
	data = envctrl_i2c_read_8591((unsigned char)pchild->addr,
				     (unsigned char)pchild->chnl_array[i].chnl_no);

	/* Find decoding table. */
	tbl = pchild->tables + pchild->tblprop_array[i].offset;

	return envctrl_i2c_data_translate(data, pchild->tblprop_array[i].type,
					  pchild->tblprop_array[i].scale,
					  tbl, bufdata);
}

/* Function Description: Read fan status.
 * Return : Always 1 byte. Status stored in bufdata.
 */
static int envctrl_i2c_fan_status(struct i2c_child_t *pchild,
				  unsigned char data,
				  char *bufdata)
{
	unsigned char tmp, ret = 0;
	int i, j = 0;

	tmp = data & pchild->fan_mask;

	if (tmp == pchild->fan_mask) {
		/* All bits are on. All fans are functioning. */
		ret = ENVCTRL_ALL_FANS_GOOD;
	} else if (tmp == 0) {
		/* No bits are on. No fans are functioning. */
		ret = ENVCTRL_ALL_FANS_BAD;
	} else {
		/* Go through all channels, mark 'on' the matched bits.
		 * Notice that fan_mask may have discontiguous bits but
		 * return mask are always contiguous. For example if we
		 * monitor 4 fans at channels 0,1,2,4, the return mask
		 * should be 00010000 if only fan at channel 4 is working.
		 */
		for (i = 0; i < PCF8584_MAX_CHANNELS;i++) {
			if (pchild->fan_mask & chnls_mask[i]) {
				if (!(chnls_mask[i] & tmp))
					ret |= chnls_mask[j];

				j++;
			}
		}
	}

	bufdata[0] = ret;
	return 1;
}

/* Function Description: Read global addressing line.
 * Return : Always 1 byte. Status stored in bufdata.
 */
static int envctrl_i2c_globaladdr(struct i2c_child_t *pchild,
				  unsigned char data,
				  char *bufdata)
{
	/* Translatation table is not necessary, as global
	 * addr is the integer value of the GA# bits.
	 *
	 * NOTE: MSB is documented as zero, but I see it as '1' always....
	 *
	 * -----------------------------------------------
	 * | 0 | FAL | DEG | GA4 | GA3 | GA2 | GA1 | GA0 |
	 * -----------------------------------------------
	 * GA0 - GA4	integer value of Global Address (backplane slot#)
	 * DEG			0 = cPCI Power supply output is starting to degrade
	 * 				1 = cPCI Power supply output is OK
	 * FAL			0 = cPCI Power supply has failed
	 * 				1 = cPCI Power supply output is OK
	 */
	bufdata[0] = (data & ENVCTRL_GLOBALADDR_ADDR_MASK);
	return 1;
}

/* Function Description: Read standard voltage and power supply status.
 * Return : Always 1 byte. Status stored in bufdata.
 */
static unsigned char envctrl_i2c_voltage_status(struct i2c_child_t *pchild,
						unsigned char data,
						char *bufdata)
{
	unsigned char tmp, ret = 0;
	int i, j = 0;

	tmp = data & pchild->voltage_mask;

	/* Two channels are used to monitor voltage and power supply. */
	if (tmp == pchild->voltage_mask) {
		/* All bits are on. Voltage and power supply are okay. */
		ret = ENVCTRL_VOLTAGE_POWERSUPPLY_GOOD;
	} else if (tmp == 0) {
		/* All bits are off. Voltage and power supply are bad */
		ret = ENVCTRL_VOLTAGE_POWERSUPPLY_BAD;
	} else {
		/* Either voltage or power supply has problem. */
		for (i = 0; i < PCF8584_MAX_CHANNELS; i++) {
			if (pchild->voltage_mask & chnls_mask[i]) {
				j++;

				/* Break out when there is a mismatch. */
				if (!(chnls_mask[i] & tmp))
					break; 
			}
		}

		/* Make a wish that hardware will always use the
		 * first channel for voltage and the second for
		 * power supply.
		 */
		if (j == 1)
			ret = ENVCTRL_VOLTAGE_BAD;
		else
			ret = ENVCTRL_POWERSUPPLY_BAD;
	}

	bufdata[0] = ret;
	return 1;
}

/* Function Description: Read a byte from /dev/envctrl. Mapped to user read().
 * Return: Number of read bytes. 0 for error.
 */
static ssize_t
envctrl_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct i2c_child_t *pchild;
	unsigned char data[10];
	int ret = 0;

	/* Get the type of read as decided in ioctl() call.
	 * Find the appropriate i2c child.
	 * Get the data and put back to the user buffer.
	 */

	switch ((int)(long)file->private_data) {
	case ENVCTRL_RD_WARNING_TEMPERATURE:
		if (warning_temperature == 0)
			return 0;

		data[0] = (unsigned char)(warning_temperature);
		ret = 1;
		if (copy_to_user(buf, data, ret))
			ret = -EFAULT;
		break;

	case ENVCTRL_RD_SHUTDOWN_TEMPERATURE:
		if (shutdown_temperature == 0)
			return 0;

		data[0] = (unsigned char)(shutdown_temperature);
		ret = 1;
		if (copy_to_user(buf, data, ret))
			ret = -EFAULT;
		break;

	case ENVCTRL_RD_MTHRBD_TEMPERATURE:
		if (!(pchild = envctrl_get_i2c_child(ENVCTRL_MTHRBDTEMP_MON)))
			return 0;
		ret = envctrl_read_noncpu_info(pchild, ENVCTRL_MTHRBDTEMP_MON, data);
		if (copy_to_user(buf, data, ret))
			ret = -EFAULT;
		break;

	case ENVCTRL_RD_CPU_TEMPERATURE:
		if (!(pchild = envctrl_get_i2c_child(ENVCTRL_CPUTEMP_MON)))
			return 0;
		ret = envctrl_read_cpu_info(read_cpu, pchild, ENVCTRL_CPUTEMP_MON, data);

		/* Reset cpu to the default cpu0. */
		if (copy_to_user(buf, data, ret))
			ret = -EFAULT;
		break;

	case ENVCTRL_RD_CPU_VOLTAGE:
		if (!(pchild = envctrl_get_i2c_child(ENVCTRL_CPUVOLTAGE_MON)))
			return 0;
		ret = envctrl_read_cpu_info(read_cpu, pchild, ENVCTRL_CPUVOLTAGE_MON, data);

		/* Reset cpu to the default cpu0. */
		if (copy_to_user(buf, data, ret))
			ret = -EFAULT;
		break;

	case ENVCTRL_RD_SCSI_TEMPERATURE:
		if (!(pchild = envctrl_get_i2c_child(ENVCTRL_SCSITEMP_MON)))
			return 0;
		ret = envctrl_read_noncpu_info(pchild, ENVCTRL_SCSITEMP_MON, data);
		if (copy_to_user(buf, data, ret))
			ret = -EFAULT;
		break;

	case ENVCTRL_RD_ETHERNET_TEMPERATURE:
		if (!(pchild = envctrl_get_i2c_child(ENVCTRL_ETHERTEMP_MON)))
			return 0;
		ret = envctrl_read_noncpu_info(pchild, ENVCTRL_ETHERTEMP_MON, data);
		if (copy_to_user(buf, data, ret))
			ret = -EFAULT;
		break;

	case ENVCTRL_RD_FAN_STATUS:
		if (!(pchild = envctrl_get_i2c_child(ENVCTRL_FANSTAT_MON)))
			return 0;
		data[0] = envctrl_i2c_read_8574(pchild->addr);
		ret = envctrl_i2c_fan_status(pchild,data[0], data);
		if (copy_to_user(buf, data, ret))
			ret = -EFAULT;
		break;
	
	case ENVCTRL_RD_GLOBALADDRESS:
		if (!(pchild = envctrl_get_i2c_child(ENVCTRL_GLOBALADDR_MON)))
			return 0;
		data[0] = envctrl_i2c_read_8574(pchild->addr);
		ret = envctrl_i2c_globaladdr(pchild, data[0], data);
		if (copy_to_user(buf, data, ret))
			ret = -EFAULT;
		break;

	case ENVCTRL_RD_VOLTAGE_STATUS:
		if (!(pchild = envctrl_get_i2c_child(ENVCTRL_VOLTAGESTAT_MON)))
			/* If voltage monitor not present, check for CPCI equivalent */
			if (!(pchild = envctrl_get_i2c_child(ENVCTRL_GLOBALADDR_MON)))
				return 0;
		data[0] = envctrl_i2c_read_8574(pchild->addr);
		ret = envctrl_i2c_voltage_status(pchild, data[0], data);
		if (copy_to_user(buf, data, ret))
			ret = -EFAULT;
		break;

	default:
		break;

	};

	return ret;
}

/* Function Description: Command what to read.  Mapped to user ioctl().
 * Return: Gives 0 for implemented commands, -EINVAL otherwise.
 */
static int
envctrl_ioctl(struct inode *inode, struct file *file,
	      unsigned int cmd, unsigned long arg)
{
	char __user *infobuf;

	switch (cmd) {
	case ENVCTRL_RD_WARNING_TEMPERATURE:
	case ENVCTRL_RD_SHUTDOWN_TEMPERATURE:
	case ENVCTRL_RD_MTHRBD_TEMPERATURE:
	case ENVCTRL_RD_FAN_STATUS:
	case ENVCTRL_RD_VOLTAGE_STATUS:
	case ENVCTRL_RD_ETHERNET_TEMPERATURE:
	case ENVCTRL_RD_SCSI_TEMPERATURE:
	case ENVCTRL_RD_GLOBALADDRESS:
		file->private_data = (void *)(long)cmd;
		break;

	case ENVCTRL_RD_CPU_TEMPERATURE:
	case ENVCTRL_RD_CPU_VOLTAGE:
		/* Check to see if application passes in any cpu number,
		 * the default is cpu0.
		 */
		infobuf = (char __user *) arg;
		if (infobuf == NULL) {
			read_cpu = 0;
		}else {
			get_user(read_cpu, infobuf);
		}

		/* Save the command for use when reading. */
		file->private_data = (void *)(long)cmd;
		break;

	default:
		return -EINVAL;
	};

	return 0;
}

/* Function Description: open device. Mapped to user open().
 * Return: Always 0.
 */
static int
envctrl_open(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

/* Function Description: Open device. Mapped to user close().
 * Return: Always 0.
 */
static int
envctrl_release(struct inode *inode, struct file *file)
{
	return 0;
}

static struct file_operations envctrl_fops = {
	.owner =	THIS_MODULE,
	.read =		envctrl_read,
	.ioctl =	envctrl_ioctl,
	.open =		envctrl_open,
	.release =	envctrl_release,
};	

static struct miscdevice envctrl_dev = {
	ENVCTRL_MINOR,
	"envctrl",
	&envctrl_fops
};

/* Function Description: Set monitor type based on firmware description.
 * Return: None.
 */
static void envctrl_set_mon(struct i2c_child_t *pchild,
			    char *chnl_desc,
			    int chnl_no)
{
	/* Firmware only has temperature type.  It does not distinguish
	 * different kinds of temperatures.  We use channel description
	 * to disinguish them.
	 */
	if (!(strcmp(chnl_desc,"temp,cpu")) ||
	    !(strcmp(chnl_desc,"temp,cpu0")) ||
	    !(strcmp(chnl_desc,"temp,cpu1")) ||
	    !(strcmp(chnl_desc,"temp,cpu2")) ||
	    !(strcmp(chnl_desc,"temp,cpu3")))
		pchild->mon_type[chnl_no] = ENVCTRL_CPUTEMP_MON;

	if (!(strcmp(chnl_desc,"vddcore,cpu0")) ||
	    !(strcmp(chnl_desc,"vddcore,cpu1")) ||
	    !(strcmp(chnl_desc,"vddcore,cpu2")) ||
	    !(strcmp(chnl_desc,"vddcore,cpu3")))
		pchild->mon_type[chnl_no] = ENVCTRL_CPUVOLTAGE_MON;

	if (!(strcmp(chnl_desc,"temp,motherboard")))
		pchild->mon_type[chnl_no] = ENVCTRL_MTHRBDTEMP_MON;

	if (!(strcmp(chnl_desc,"temp,scsi")))
		pchild->mon_type[chnl_no] = ENVCTRL_SCSITEMP_MON;

	if (!(strcmp(chnl_desc,"temp,ethernet")))
		pchild->mon_type[chnl_no] = ENVCTRL_ETHERTEMP_MON;
}

/* Function Description: Initialize monitor channel with channel desc,
 *                       decoding tables, monitor type, optional properties.
 * Return: None.
 */
static void envctrl_init_adc(struct i2c_child_t *pchild, int node)
{
	char chnls_desc[CHANNEL_DESC_SZ];
	int i = 0, len;
	char *pos = chnls_desc;

	/* Firmware describe channels into a stream separated by a '\0'. */
	len = prom_getproperty(node, "channels-description", chnls_desc,
			       CHANNEL_DESC_SZ);
	chnls_desc[CHANNEL_DESC_SZ - 1] = '\0';

	while (len > 0) {
		int l = strlen(pos) + 1;
		envctrl_set_mon(pchild, pos, i++);
		len -= l;
		pos += l;
	}

	/* Get optional properties. */
        len = prom_getproperty(node, "warning-temp", (char *)&warning_temperature,
			       sizeof(warning_temperature));
        len = prom_getproperty(node, "shutdown-temp", (char *)&shutdown_temperature,
			       sizeof(shutdown_temperature));
}

/* Function Description: Initialize child device monitoring fan status.
 * Return: None.
 */
static void envctrl_init_fanstat(struct i2c_child_t *pchild)
{
	int i;

	/* Go through all channels and set up the mask. */
	for (i = 0; i < pchild->total_chnls; i++)
		pchild->fan_mask |= chnls_mask[(pchild->chnl_array[i]).chnl_no];

	/* We only need to know if this child has fan status monitored.
	 * We don't care which channels since we have the mask already.
	 */
	pchild->mon_type[0] = ENVCTRL_FANSTAT_MON;
}

/* Function Description: Initialize child device for global addressing line.
 * Return: None.
 */
static void envctrl_init_globaladdr(struct i2c_child_t *pchild)
{
	int i;

	/* Voltage/PowerSupply monitoring is piggybacked 
	 * with Global Address on CompactPCI.  See comments
	 * within envctrl_i2c_globaladdr for bit assignments.
	 *
	 * The mask is created here by assigning mask bits to each
	 * bit position that represents PCF8584_VOLTAGE_TYPE data.
	 * Channel numbers are not consecutive within the globaladdr
	 * node (why?), so we use the actual counter value as chnls_mask
	 * index instead of the chnl_array[x].chnl_no value.
	 *
	 * NOTE: This loop could be replaced with a constant representing
	 * a mask of bits 5&6 (ENVCTRL_GLOBALADDR_PSTAT_MASK).
	 */
	for (i = 0; i < pchild->total_chnls; i++) {
		if (PCF8584_VOLTAGE_TYPE == pchild->chnl_array[i].type) {
			pchild->voltage_mask |= chnls_mask[i];
		}
	}

	/* We only need to know if this child has global addressing 
	 * line monitored.  We don't care which channels since we know 
	 * the mask already (ENVCTRL_GLOBALADDR_ADDR_MASK).
	 */
	pchild->mon_type[0] = ENVCTRL_GLOBALADDR_MON;
}

/* Initialize child device monitoring voltage status. */
static void envctrl_init_voltage_status(struct i2c_child_t *pchild)
{
	int i;

	/* Go through all channels and set up the mask. */
	for (i = 0; i < pchild->total_chnls; i++)
		pchild->voltage_mask |= chnls_mask[(pchild->chnl_array[i]).chnl_no];

	/* We only need to know if this child has voltage status monitored.
	 * We don't care which channels since we have the mask already.
	 */
	pchild->mon_type[0] = ENVCTRL_VOLTAGESTAT_MON;
}

/* Function Description: Initialize i2c child device.
 * Return: None.
 */
static void envctrl_init_i2c_child(struct linux_ebus_child *edev_child,
				   struct i2c_child_t *pchild)
{
	int node, len, i, tbls_size = 0;

	node = edev_child->prom_node;

	/* Get device address. */
	len = prom_getproperty(node, "reg",
			       (char *) &(pchild->addr),
			       sizeof(pchild->addr));

	/* Get tables property.  Read firmware temperature tables. */
	len = prom_getproperty(node, "translation",
			       (char *) pchild->tblprop_array,
			       (PCF8584_MAX_CHANNELS *
				sizeof(struct pcf8584_tblprop)));
	if (len > 0) {
                pchild->total_tbls = len / sizeof(struct pcf8584_tblprop);
		for (i = 0; i < pchild->total_tbls; i++) {
			if ((pchild->tblprop_array[i].size + pchild->tblprop_array[i].offset) > tbls_size) {
				tbls_size = pchild->tblprop_array[i].size + pchild->tblprop_array[i].offset;
			}
		}

                pchild->tables = kmalloc(tbls_size, GFP_KERNEL);
		if (pchild->tables == NULL){
			printk("envctrl: Failed to allocate table.\n");
			return;
		}
                len = prom_getproperty(node, "tables",
				       (char *) pchild->tables, tbls_size);
                if (len <= 0) {
			printk("envctrl: Failed to get table.\n");
			return;
		}
	}

	/* SPARCengine ASM Reference Manual (ref. SMI doc 805-7581-04)
	 * sections 2.5, 3.5, 4.5 state node 0x70 for CP1400/1500 is
	 * "For Factory Use Only."
	 *
	 * We ignore the node on these platforms by assigning the
	 * 'NULL' monitor type.
	 */
	if (ENVCTRL_CPCI_IGNORED_NODE == pchild->addr) {
		int len;
		char prop[56];

		len = prom_getproperty(prom_root_node, "name", prop, sizeof(prop));
		if (0 < len && (0 == strncmp(prop, "SUNW,UltraSPARC-IIi-cEngine", len)))
		{
			for (len = 0; len < PCF8584_MAX_CHANNELS; ++len) {
				pchild->mon_type[len] = ENVCTRL_NOMON;
			}
			return;
		}
	}

	/* Get the monitor channels. */
	len = prom_getproperty(node, "channels-in-use",
			       (char *) pchild->chnl_array,
			       (PCF8584_MAX_CHANNELS *
				sizeof(struct pcf8584_channel)));
	pchild->total_chnls = len / sizeof(struct pcf8584_channel);

	for (i = 0; i < pchild->total_chnls; i++) {
		switch (pchild->chnl_array[i].type) {
		case PCF8584_TEMP_TYPE:
			envctrl_init_adc(pchild, node);
			break;

		case PCF8584_GLOBALADDR_TYPE:
			envctrl_init_globaladdr(pchild);
			i = pchild->total_chnls;
			break;

		case PCF8584_FANSTAT_TYPE:
			envctrl_init_fanstat(pchild);
			i = pchild->total_chnls;
			break;

		case PCF8584_VOLTAGE_TYPE:
			if (pchild->i2ctype == I2C_ADC) {
				envctrl_init_adc(pchild,node);
			} else {
				envctrl_init_voltage_status(pchild);
			}
			i = pchild->total_chnls;
			break;

		default:
			break;
		};
	}
}

/* Function Description: Search the child device list for a device.
 * Return : The i2c child if found. NULL otherwise.
 */
static struct i2c_child_t *envctrl_get_i2c_child(unsigned char mon_type)
{
	int i, j;

	for (i = 0; i < ENVCTRL_MAX_CPU*2; i++) {
		for (j = 0; j < PCF8584_MAX_CHANNELS; j++) {
			if (i2c_childlist[i].mon_type[j] == mon_type) {
				return (struct i2c_child_t *)(&(i2c_childlist[i]));
			}
		}
	}
	return NULL;
}

static void envctrl_do_shutdown(void)
{
	static int inprog = 0;
	static char *envp[] = {	
		"HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
	char *argv[] = { 
		"/sbin/shutdown", "-h", "now", NULL };	

	if (inprog != 0)
		return;

	inprog = 1;
	printk(KERN_CRIT "kenvctrld: WARNING: Shutting down the system now.\n");
	if (0 > execve("/sbin/shutdown", argv, envp)) {
		printk(KERN_CRIT "kenvctrld: WARNING: system shutdown failed!\n"); 
		inprog = 0;  /* unlikely to succeed, but we could try again */
	}
}

static struct task_struct *kenvctrld_task;

static int kenvctrld(void *__unused)
{
	int poll_interval;
	int whichcpu;
	char tempbuf[10];
	struct i2c_child_t *cputemp;

	if (NULL == (cputemp = envctrl_get_i2c_child(ENVCTRL_CPUTEMP_MON))) {
		printk(KERN_ERR 
		       "envctrl: kenvctrld unable to monitor CPU temp-- exiting\n");
		return -ENODEV;
	}

	poll_interval = 5 * HZ; /* TODO env_mon_interval */

	daemonize("kenvctrld");
	allow_signal(SIGKILL);

	kenvctrld_task = current;

	printk(KERN_INFO "envctrl: %s starting...\n", current->comm);
	for (;;) {
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(poll_interval);

		if(signal_pending(current))
			break;

		for (whichcpu = 0; whichcpu < ENVCTRL_MAX_CPU; ++whichcpu) {
			if (0 < envctrl_read_cpu_info(whichcpu, cputemp,
						      ENVCTRL_CPUTEMP_MON,
						      tempbuf)) {
				if (tempbuf[0] >= shutdown_temperature) {
					printk(KERN_CRIT 
						"%s: WARNING: CPU%i temperature %i C meets or exceeds "\
						"shutdown threshold %i C\n", 
						current->comm, whichcpu, 
						tempbuf[0], shutdown_temperature);
					envctrl_do_shutdown();
				}
			}
		}
	}
	printk(KERN_INFO "envctrl: %s exiting...\n", current->comm);
	return 0;
}

static int __init envctrl_init(void)
{
#ifdef CONFIG_PCI
	struct linux_ebus *ebus = NULL;
	struct linux_ebus_device *edev = NULL;
	struct linux_ebus_child *edev_child = NULL;
	int err, i = 0;

	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (!strcmp(edev->prom_name, "bbc")) {
				/* If we find a boot-bus controller node,
				 * then this envctrl driver is not for us.
				 */
				return -ENODEV;
			}
		}
	}

	/* Traverse through ebus and ebus device list for i2c device and
	 * adc and gpio nodes.
	 */
	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (!strcmp(edev->prom_name, "i2c")) {
				i2c = ioremap(edev->resource[0].start, 0x2);
				for_each_edevchild(edev, edev_child) {
					if (!strcmp("gpio", edev_child->prom_name)) {
						i2c_childlist[i].i2ctype = I2C_GPIO;
						envctrl_init_i2c_child(edev_child, &(i2c_childlist[i++]));
					}
					if (!strcmp("adc", edev_child->prom_name)) {
						i2c_childlist[i].i2ctype = I2C_ADC;
						envctrl_init_i2c_child(edev_child, &(i2c_childlist[i++]));
					}
				}
				goto done;
			}
		}
	}

done:
	if (!edev) {
		printk("envctrl: I2C device not found.\n");
		return -ENODEV;
	}

	/* Set device address. */
	writeb(CONTROL_PIN, i2c + PCF8584_CSR);
	writeb(PCF8584_ADDRESS, i2c + PCF8584_DATA);

	/* Set system clock and SCL frequencies. */ 
	writeb(CONTROL_PIN | CONTROL_ES1, i2c + PCF8584_CSR);
	writeb(CLK_4_43 | BUS_CLK_90, i2c + PCF8584_DATA);

	/* Enable serial interface. */
	writeb(CONTROL_PIN | CONTROL_ES0 | CONTROL_ACK, i2c + PCF8584_CSR);
	udelay(200);

	/* Register the device as a minor miscellaneous device. */
	err = misc_register(&envctrl_dev);
	if (err) {
		printk("envctrl: Unable to get misc minor %d\n",
		       envctrl_dev.minor);
		goto out_iounmap;
	}

	/* Note above traversal routine post-incremented 'i' to accommodate 
	 * a next child device, so we decrement before reverse-traversal of
	 * child devices.
	 */
	printk("envctrl: initialized ");
	for (--i; i >= 0; --i) {
		printk("[%s 0x%lx]%s", 
			(I2C_ADC == i2c_childlist[i].i2ctype) ? ("adc") : 
			((I2C_GPIO == i2c_childlist[i].i2ctype) ? ("gpio") : ("unknown")), 
			i2c_childlist[i].addr, (0 == i) ? ("\n") : (" "));
	}

	err = kernel_thread(kenvctrld, NULL, CLONE_FS | CLONE_FILES);
	if (err < 0)
		goto out_deregister;

	return 0;

out_deregister:
	misc_deregister(&envctrl_dev);
out_iounmap:
	iounmap(i2c);
	for (i = 0; i < ENVCTRL_MAX_CPU * 2; i++) {
		if (i2c_childlist[i].tables)
			kfree(i2c_childlist[i].tables);
	}
	return err;
#else
	return -ENODEV;
#endif
}

static void __exit envctrl_cleanup(void)
{
	int i;

	if (NULL != kenvctrld_task) {
		force_sig(SIGKILL, kenvctrld_task);
		for (;;) {
			struct task_struct *p;
			int found = 0;

			read_lock(&tasklist_lock);
			for_each_process(p) {
				if (p == kenvctrld_task) {
					found = 1;
					break;
				}
			}
			read_unlock(&tasklist_lock);

			if (!found)
				break;

			msleep(1000);
		}
		kenvctrld_task = NULL;
	}

	iounmap(i2c);
	misc_deregister(&envctrl_dev);

	for (i = 0; i < ENVCTRL_MAX_CPU * 2; i++) {
		if (i2c_childlist[i].tables)
			kfree(i2c_childlist[i].tables);
	}
}

module_init(envctrl_init);
module_exit(envctrl_cleanup);
MODULE_LICENSE("GPL");

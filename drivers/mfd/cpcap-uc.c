/*
 * Copyright (C) 2008-2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/ihex.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>

#include <linux/spi/cpcap.h>
#include <linux/spi/cpcap-regbits.h>
#include <linux/spi/spi.h>


#define ERROR_MACRO_TIMEOUT  0x81
#define ERROR_MACRO_WRITE    0x82
#define ERROR_MACRO_READ     0x83

#define RAM_START_TI         0x9000
#define RAM_END_TI           0x9FA0
#define RAM_START_ST         0x0000
#define RAM_END_ST           0x0FFF

enum {
	READ_STATE_1,	/* Send size and location of RAM read. */
	READ_STATE_2,   /*!< Read MT registers. */
	READ_STATE_3,   /*!< Read data from uC. */
	READ_STATE_4,   /*!< Check for error. */
};

enum {
	WRITE_STATE_1,	/* Send size and location of RAM write. */
	WRITE_STATE_2,	/* Check for error. */
	WRITE_STATE_3,	/* Write data to uC. */
	WRITE_STATE_4	/* Check for error. */
};

struct cpcap_uc_data {
	struct cpcap_device *cpcap;
	unsigned char is_supported;
	unsigned char is_ready;
	struct completion completion;
	int cb_status;
	struct mutex lock;
	unsigned char uc_reset;
	unsigned char state;
	unsigned short state_cntr;
	struct {
		unsigned short address;
		unsigned short *data;
		unsigned short num_words;
	} req;
};

static struct cpcap_uc_data *cpcap_uc_info;

static int fops_open(struct inode *inode, struct file *file);
static int fops_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg);
static ssize_t fops_write(struct file *file, const char *buf,
			  size_t count, loff_t *ppos);
static ssize_t fops_read(struct file *file, char *buf,
			 size_t count, loff_t *ppos);


static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.ioctl = fops_ioctl,
	.open = fops_open,
	.read = fops_read,
	.write = fops_write,
};

static struct miscdevice uc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "cpcap_uc",
	.fops = &fops,
};

static int is_valid_address(struct cpcap_device *cpcap, unsigned short address,
			    unsigned short num_words)
{
	int vld = 0;

	if (cpcap->vendor == CPCAP_VENDOR_TI) {
		vld = (address >= RAM_START_TI) &&
		    ((address + num_words) <= RAM_END_TI);
	} else if (cpcap->vendor == CPCAP_VENDOR_ST) {
		vld = ((address + num_words) <= RAM_END_ST);
	}

	return vld;
}

static void ram_read_state_machine(enum cpcap_irqs irq, void *data)
{
	struct cpcap_uc_data *uc_data = data;
	unsigned short temp;

	if (irq != CPCAP_IRQ_UC_PRIRAMR)
		return;

	switch (uc_data->state) {
	case READ_STATE_1:
		cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_MT1,
				   uc_data->req.address, 0xFFFF);
		cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_MT2,
				   uc_data->req.num_words, 0xFFFF);
		cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_MT3, 0, 0xFFFF);

		if (uc_data->cpcap->vendor == CPCAP_VENDOR_ST)
			uc_data->state = READ_STATE_2;
		else
			uc_data->state = READ_STATE_3;

		cpcap_irq_unmask(uc_data->cpcap, CPCAP_IRQ_UC_PRIRAMR);
		break;

	case READ_STATE_2:
		cpcap_regacc_read(uc_data->cpcap, CPCAP_REG_MT1, &temp);

		if (temp == ERROR_MACRO_READ) {
			uc_data->state = READ_STATE_1;
			uc_data->state_cntr = 0;

			cpcap_irq_mask(uc_data->cpcap, CPCAP_IRQ_UC_PRIRAMR);

			uc_data->cb_status = -EIO;

			complete(&uc_data->completion);
		} else {
			cpcap_regacc_read(uc_data->cpcap, CPCAP_REG_MT2, &temp);
			cpcap_regacc_read(uc_data->cpcap, CPCAP_REG_MT3, &temp);

			uc_data->state = READ_STATE_3;
			cpcap_irq_unmask(uc_data->cpcap, CPCAP_IRQ_UC_PRIRAMR);
		}
		break;

	case READ_STATE_3:
		cpcap_regacc_read(uc_data->cpcap, CPCAP_REG_MT1,
				  uc_data->req.data + uc_data->state_cntr);

		uc_data->state_cntr += 1;

		if (uc_data->state_cntr == uc_data->req.num_words)
			cpcap_regacc_read(uc_data->cpcap, CPCAP_REG_MT2, &temp);
		else {
			cpcap_regacc_read(uc_data->cpcap, CPCAP_REG_MT2,
					  uc_data->req.data +
					  uc_data->state_cntr);

			uc_data->state_cntr += 1;
		}

		if (uc_data->state_cntr == uc_data->req.num_words)
			cpcap_regacc_read(uc_data->cpcap, CPCAP_REG_MT3, &temp);
		else {
			cpcap_regacc_read(uc_data->cpcap, CPCAP_REG_MT3,
					  uc_data->req.data +
					  uc_data->state_cntr);

			uc_data->state_cntr += 1;
		}

		if (uc_data->state_cntr == uc_data->req.num_words)
			uc_data->state = READ_STATE_4;

		cpcap_irq_unmask(uc_data->cpcap, CPCAP_IRQ_UC_PRIRAMR);
		break;

	case READ_STATE_4:
		cpcap_regacc_read(uc_data->cpcap, CPCAP_REG_MT1, &temp);

		if (temp != ERROR_MACRO_READ)
			uc_data->cb_status = 0;
		else
			uc_data->cb_status = -EIO;

		complete(&uc_data->completion);

		uc_data->state = READ_STATE_1;
		uc_data->state_cntr = 0;
		break;

	default:
		uc_data->state = READ_STATE_1;
		uc_data->state_cntr = 0;
		break;
	}
}

static void ram_write_state_machine(enum cpcap_irqs irq, void *data)
{
	struct cpcap_uc_data *uc_data = data;
	unsigned short error_check;

	if (irq != CPCAP_IRQ_UC_PRIRAMW)
		return;

	switch (uc_data->state) {
	case WRITE_STATE_1:
		cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_MT1,
				   uc_data->req.address, 0xFFFF);
		cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_MT2,
				   uc_data->req.num_words, 0xFFFF);
		cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_MT3, 0, 0xFFFF);

		uc_data->state = WRITE_STATE_2;
		cpcap_irq_unmask(uc_data->cpcap, CPCAP_IRQ_UC_PRIRAMW);
		break;

	case WRITE_STATE_2:
		cpcap_regacc_read(uc_data->cpcap, CPCAP_REG_MT1, &error_check);

		if (error_check == ERROR_MACRO_WRITE) {
			uc_data->state = WRITE_STATE_1;
			uc_data->state_cntr = 0;

			cpcap_irq_mask(uc_data->cpcap,
				       CPCAP_IRQ_UC_PRIRAMW);

			uc_data->cb_status = -EIO;
			complete(&uc_data->completion);
			break;
		} else
			uc_data->state = WRITE_STATE_3;

		/* No error has occured, fall through */

	case WRITE_STATE_3:
		cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_MT1,
				   *(uc_data->req.data + uc_data->state_cntr),
				   0xFFFF);
		uc_data->state_cntr += 1;

		if (uc_data->state_cntr == uc_data->req.num_words)
			cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_MT2, 0,
					   0xFFFF);
		else {
			cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_MT2,
					   *(uc_data->req.data +
					     uc_data->state_cntr), 0xFFFF);

			uc_data->state_cntr += 1;
		}

		if (uc_data->state_cntr == uc_data->req.num_words)
			cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_MT3, 0,
					   0xFFFF);
		else {
			cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_MT3,
					   *(uc_data->req.data +
					     uc_data->state_cntr), 0xFFFF);

			uc_data->state_cntr += 1;
		}

		if (uc_data->state_cntr == uc_data->req.num_words)
			uc_data->state = WRITE_STATE_4;

		cpcap_irq_unmask(uc_data->cpcap, CPCAP_IRQ_UC_PRIRAMW);
		break;

	case WRITE_STATE_4:
		cpcap_regacc_read(uc_data->cpcap, CPCAP_REG_MT1, &error_check);

		if (error_check != ERROR_MACRO_WRITE)
			uc_data->cb_status = 0;
		else
			uc_data->cb_status = -EIO;

		complete(&uc_data->completion);

		uc_data->state = WRITE_STATE_1;
		uc_data->state_cntr = 0;
		break;

	default:
		uc_data->state = WRITE_STATE_1;
		uc_data->state_cntr = 0;
		break;
	}
}

static void reset_handler(enum cpcap_irqs irq, void *data)
{
	int i;
	unsigned short regval;
	struct cpcap_uc_data *uc_data = data;

	if (irq != CPCAP_IRQ_UCRESET)
		return;

	cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_UCC1,
			   CPCAP_BIT_PRIHALT, CPCAP_BIT_PRIHALT);

	cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_PGC,
			   CPCAP_BIT_PRI_UC_SUSPEND, CPCAP_BIT_PRI_UC_SUSPEND);

	uc_data->uc_reset = 1;
	uc_data->cb_status = -EIO;
	complete(&uc_data->completion);

	cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_MI2, 0, 0xFFFF);
	cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_MIM1, 0xFFFF, 0xFFFF);
	cpcap_irq_mask(uc_data->cpcap, CPCAP_IRQ_PRIMAC);
	cpcap_irq_unmask(uc_data->cpcap, CPCAP_IRQ_UCRESET);

	for (i = 0; i <= CPCAP_REG_END; i++) {
		cpcap_regacc_read(uc_data->cpcap, i, &regval);
		dev_err(&uc_data->cpcap->spi->dev,
			"cpcap reg %d = 0x%04X\n", i, regval);
	}

	BUG();
}

static void primac_handler(enum cpcap_irqs irq, void *data)
{
	struct cpcap_uc_data *uc_data = data;

	if (irq == CPCAP_IRQ_PRIMAC)
		cpcap_irq_unmask(uc_data->cpcap, CPCAP_IRQ_PRIMAC);
}

static int ram_write(struct cpcap_uc_data *uc_data, unsigned short address,
		     unsigned short num_words, unsigned short *data)
{
	int retval = -EFAULT;

	mutex_lock(&uc_data->lock);

	if ((uc_data->cpcap->vendor == CPCAP_VENDOR_ST) &&
	    (uc_data->cpcap->revision <= CPCAP_REVISION_2_0)) {
		cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_UCTM,
				   CPCAP_BIT_UCTM, CPCAP_BIT_UCTM);
	}

	if (uc_data->is_supported && (num_words > 0) &&
		(data != NULL) &&
		is_valid_address(uc_data->cpcap, address, num_words) &&
	    !uc_data->uc_reset) {
		uc_data->req.address = address;
		uc_data->req.data = data;
		uc_data->req.num_words = num_words;
		uc_data->state = WRITE_STATE_1;
		uc_data->state_cntr = 0;
		INIT_COMPLETION(uc_data->completion);

		retval = cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_MI2,
					CPCAP_BIT_PRIRAMW,
					CPCAP_BIT_PRIRAMW);
		if (retval)
			goto err;

		/* Cannot call cpcap_irq_register() here because unregister
		 * cannot be called from the state machine. Doing so causes
		 * a deadlock. */
		retval = cpcap_irq_unmask(uc_data->cpcap, CPCAP_IRQ_UC_PRIRAMW);
		if (retval)
			goto err;

		wait_for_completion(&uc_data->completion);
		retval = uc_data->cb_status;
	}

err:
	if ((uc_data->cpcap->vendor == CPCAP_VENDOR_ST) &&
	    (uc_data->cpcap->revision <= CPCAP_REVISION_2_0)) {
		cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_UCTM,
				   0, CPCAP_BIT_UCTM);
	}

	mutex_unlock(&uc_data->lock);
	return retval;
}

static int ram_read(struct cpcap_uc_data *uc_data, unsigned short address,
		    unsigned short num_words, unsigned short *data)
{
	int retval = -EFAULT;

	mutex_lock(&uc_data->lock);

	if ((uc_data->cpcap->vendor == CPCAP_VENDOR_ST) &&
	    (uc_data->cpcap->revision <= CPCAP_REVISION_2_0)) {
		cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_UCTM,
				   CPCAP_BIT_UCTM, CPCAP_BIT_UCTM);
	}

	if (uc_data->is_supported && (num_words > 0) &&
	    is_valid_address(uc_data->cpcap, address, num_words) &&
		!uc_data->uc_reset) {
		uc_data->req.address = address;
		uc_data->req.data = data;
		uc_data->req.num_words = num_words;
		uc_data->state = READ_STATE_1;
		uc_data->state_cntr = 0;
		INIT_COMPLETION(uc_data->completion);

		retval = cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_MI2,
					    CPCAP_BIT_PRIRAMR,
					    CPCAP_BIT_PRIRAMR);
		if (retval)
			goto err;

		/* Cannot call cpcap_irq_register() here because unregister
		 * cannot be called from the state machine. Doing so causes
		 * a deadlock. */
		retval = cpcap_irq_unmask(uc_data->cpcap, CPCAP_IRQ_UC_PRIRAMR);
		if (retval)
			goto err;

		wait_for_completion(&uc_data->completion);
		retval = uc_data->cb_status;
	}

err:
	if ((uc_data->cpcap->vendor == CPCAP_VENDOR_ST) &&
	    (uc_data->cpcap->revision <= CPCAP_REVISION_2_0)) {
		cpcap_regacc_write(uc_data->cpcap, CPCAP_REG_UCTM,
				   0, CPCAP_BIT_UCTM);
	}

	mutex_unlock(&uc_data->lock);
	return retval;
}

static int ram_load(struct cpcap_uc_data *uc_data, unsigned int num_words,
		    unsigned short *data)
{
	int retval = -EINVAL;

	if ((data != NULL) && (num_words > 0))
		retval = ram_write(uc_data, data[0], (num_words - 1),
				   (data + 1));

	return retval;
}

static ssize_t fops_write(struct file *file, const char *buf,
			  size_t count, loff_t *ppos)
{
	ssize_t retval = -EINVAL;
	unsigned short address;
	unsigned short num_words;
	unsigned short *data;
	struct cpcap_uc_data *uc_data = file->private_data;

	if ((buf != NULL) && (ppos != NULL) && (count >= 2)) {
		data = kzalloc(count, GFP_KERNEL);

		if (data != NULL) {
			num_words = (unsigned short) (count >> 1);

			/* If the position (uC RAM address) is zero then the
			 * data contains the address */
			if (*ppos == 0) {
				if (copy_from_user((void *) data, (void *) buf,
						   count) == 0)
					retval = ram_load(uc_data, num_words,
							  data);
				else
					retval = -EFAULT;
			}
			/* If the position (uC RAM address) is not zero then the
			 * position holds the address to load the data */
			else {
				address = (unsigned short) (*ppos);

				if (copy_from_user((void *) data, (void *) buf,
						   count) == 0)
					retval = ram_write(uc_data, address,
							   num_words, data);
				else
					retval = -EFAULT;
			}

			kfree(data);
		} else {
			retval = -ENOMEM;
		}
	}

	if (retval == 0)
		retval = num_words;

	return retval;
}

static ssize_t fops_read(struct file *file, char *buf,
			 size_t count, loff_t *ppos)
{
	ssize_t retval = -EFAULT;
	unsigned short address;
	unsigned short num_words;
	unsigned short *data;
	struct cpcap_uc_data *uc_data = file->private_data;

	if ((buf != NULL) && (ppos != NULL) && (count >= 2)) {
		data = kzalloc(count, GFP_KERNEL);

		if (data != NULL) {
			address = (unsigned short) (*ppos);
			num_words = (unsigned short) (count >> 1);

			retval = ram_read(uc_data, address, num_words, data);
			if (retval)
				goto err;

			if (copy_to_user((void *)buf, (void *)data, count) == 0)
				retval = count;
			else
				retval = -EFAULT;

err:
			kfree(data);
		} else {
			retval = -ENOMEM;
		}
	}

	return retval;
}

static int fops_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	int retval = -ENOTTY;
	struct cpcap_uc_data *data = file->private_data;

	switch (cmd) {
	case CPCAP_IOCTL_UC_MACRO_START:
		/* User space will only attempt to start the init macro if
		 * the ram load requests complete successfully. This is used
		 * as an indication that kernel requests to start macros can
		 * be allowed.
		 */
		data->is_ready = 1;

		retval = cpcap_uc_start(data->cpcap, (enum cpcap_macro)arg);

		break;

	case CPCAP_IOCTL_UC_MACRO_STOP:
		retval = cpcap_uc_stop(data->cpcap, (enum cpcap_macro)arg);
		break;

	case CPCAP_IOCTL_UC_GET_VENDOR:
		retval = copy_to_user((enum cpcap_vendor *)arg,
					&(data->cpcap->vendor),
					sizeof(enum cpcap_vendor));
		break;

	case CPCAP_IOCTL_UC_SET_TURBO_MODE:
		if (arg != 0)
			arg = 1;
		retval = cpcap_regacc_write(data->cpcap, CPCAP_REG_UCTM,
					(unsigned short)arg,
					CPCAP_BIT_UCTM);
		break;

	default:
		break;
	}

	return retval;
}

static int fops_open(struct inode *inode, struct file *file)
{
	int retval = -ENOTTY;

	if (cpcap_uc_info->is_supported)
		retval = 0;

	file->private_data = cpcap_uc_info;
	dev_info(&cpcap_uc_info->cpcap->spi->dev, "CPCAP uC: open status:%d\n",
		 retval);

	return retval;
}

int cpcap_uc_start(struct cpcap_device *cpcap, enum cpcap_macro macro)
{
	int retval = -EFAULT;
	struct cpcap_uc_data *data = cpcap->ucdata;

	if ((data->is_ready) &&
	    (macro > CPCAP_MACRO_USEROFF) && (macro < CPCAP_MACRO__END) &&
	    (data->uc_reset == 0)) {
		if ((macro == CPCAP_MACRO_4) ||
		    ((cpcap->vendor == CPCAP_VENDOR_ST) &&
		     ((macro == CPCAP_MACRO_12) || (macro == CPCAP_MACRO_14) ||
		      (macro == CPCAP_MACRO_15)))) {
			retval = cpcap_regacc_write(cpcap, CPCAP_REG_MI2,
						    (1 << macro),
						    (1 << macro));
		} else {
			retval = cpcap_regacc_write(cpcap, CPCAP_REG_MIM1,
						    0, (1 << macro));
		}
	}

	return retval;
}
EXPORT_SYMBOL_GPL(cpcap_uc_start);

int cpcap_uc_stop(struct cpcap_device *cpcap, enum cpcap_macro macro)
{
	int retval = -EFAULT;

	if ((macro > CPCAP_MACRO_4) &&
	    (macro < CPCAP_MACRO__END)) {
		if ((cpcap->vendor == CPCAP_VENDOR_ST) &&
		    ((macro == CPCAP_MACRO_12) || (macro == CPCAP_MACRO_14) ||
		     (macro == CPCAP_MACRO_15))) {
			retval = cpcap_regacc_write(cpcap, CPCAP_REG_MI2,
						    0, (1 << macro));
		} else {
			retval = cpcap_regacc_write(cpcap, CPCAP_REG_MIM1,
						    (1 << macro), (1 << macro));
		}
	}

	return retval;
}
EXPORT_SYMBOL_GPL(cpcap_uc_stop);

unsigned char cpcap_uc_status(struct cpcap_device *cpcap,
			      enum cpcap_macro macro)
{
	unsigned char retval = 0;
	unsigned short regval;

	if (macro < CPCAP_MACRO__END) {
		if ((macro <= CPCAP_MACRO_4) ||
		    ((cpcap->vendor == CPCAP_VENDOR_ST) &&
		     ((macro == CPCAP_MACRO_12) || (macro == CPCAP_MACRO_14) ||
		      (macro == CPCAP_MACRO_15)))) {
			cpcap_regacc_read(cpcap, CPCAP_REG_MI2, &regval);

			if (regval & (1 << macro))
				retval = 1;
		} else {
			cpcap_regacc_read(cpcap, CPCAP_REG_MIM1, &regval);

			if (!(regval & (1 << macro)))
				retval = 1;
		}
	}

	return retval;
}
EXPORT_SYMBOL_GPL(cpcap_uc_status);

static int fw_load(struct cpcap_uc_data *uc_data, struct device *dev)
{
	int err;
	const struct ihex_binrec *rec;
	const struct firmware *fw;
	unsigned short *buf;
	int i;
	unsigned short num_bytes;
	unsigned short num_words;
	unsigned char odd_bytes;

	if (!uc_data || !dev)
		return -EINVAL;

	if (uc_data->cpcap->vendor == CPCAP_VENDOR_ST)
		err = request_ihex_firmware(&fw, "cpcap/firmware_0_2x.fw", dev);
	else
		err = request_ihex_firmware(&fw, "cpcap/firmware_1_2x.fw", dev);

	if (err) {
		dev_err(dev, "Failed to load \"cpcap/firmware_%d_2x.fw\": %d\n",
			uc_data->cpcap->vendor, err);
		goto err;
	}

	for (rec = (void *)fw->data; rec; rec = ihex_next_binrec(rec)) {
		odd_bytes = 0;
		num_bytes = be16_to_cpu(rec->len);

		/* Since loader requires words, need even number of bytes. */
		if (be16_to_cpu(rec->len) % 2) {
			num_bytes++;
			odd_bytes = 1;
		}

		num_words = num_bytes >> 1;
		dev_info(dev, "Loading %d word(s) at 0x%04x\n",
			 num_words, be32_to_cpu(rec->addr));

		buf = kzalloc(num_bytes, GFP_KERNEL);
		if (buf) {
			for (i = 0; i < num_words; i++) {
				if (odd_bytes && (i == (num_words - 1)))
					buf[i] = rec->data[i * 2];
				else
					buf[i] = ((uint16_t *)rec->data)[i];

				buf[i] = be16_to_cpu(buf[i]);
			}

			err = ram_write(uc_data, be32_to_cpu(rec->addr),
					num_words, buf);
			kfree(buf);

			if (err) {
				dev_err(dev, "RAM write failed: %d\n", err);
				break;
			}
		} else {
			err = -ENOMEM;
			dev_err(dev, "RAM write failed: %d\n", err);
			break;
		}
	}

	release_firmware(fw);

	if (!err) {
		uc_data->is_ready = 1;

		err = cpcap_uc_start(uc_data->cpcap, CPCAP_MACRO_4);
		dev_info(dev, "Started macro 4: %d\n", err);
	}

err:
	return err;
}

static int cpcap_uc_probe(struct platform_device *pdev)
{
	int retval = 0;
	struct cpcap_uc_data *data;

	if (pdev->dev.platform_data == NULL) {
		dev_err(&pdev->dev, "no platform_data\n");
		return -EINVAL;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->cpcap = pdev->dev.platform_data;
	data->uc_reset = 0;
	data->is_supported = 0;
	data->req.address = 0;
	data->req.data = NULL;
	data->req.num_words = 0;

	init_completion(&data->completion);
	mutex_init(&data->lock);
	platform_set_drvdata(pdev, data);
	cpcap_uc_info = data;
	data->cpcap->ucdata = data;

	if (((data->cpcap->vendor == CPCAP_VENDOR_TI) &&
	     (data->cpcap->revision >= CPCAP_REVISION_2_0)) ||
		(data->cpcap->vendor == CPCAP_VENDOR_ST)) {
		retval = cpcap_irq_register(data->cpcap, CPCAP_IRQ_PRIMAC,
					    primac_handler, data);
		if (retval)
			goto err_free;

		cpcap_irq_clear(data->cpcap, CPCAP_IRQ_UCRESET);
		retval = cpcap_irq_register(data->cpcap, CPCAP_IRQ_UCRESET,
					    reset_handler, data);
		if (retval)
			goto err_primac;

		retval = cpcap_irq_register(data->cpcap,
					    CPCAP_IRQ_UC_PRIRAMR,
					    ram_read_state_machine, data);
		if (retval)
			goto err_ucreset;

		retval = cpcap_irq_register(data->cpcap,
					    CPCAP_IRQ_UC_PRIRAMW,
					    ram_write_state_machine, data);
		if (retval)
			goto err_priramr;

		retval = misc_register(&uc_dev);
		if (retval)
			goto err_priramw;

		data->is_supported = 1;

		cpcap_regacc_write(data->cpcap, CPCAP_REG_MIM1, 0xFFFF,
				   0xFFFF);

		retval = fw_load(data, &pdev->dev);
		if (retval)
			goto err_fw;
	} else
		retval = -ENODEV;

	return retval;

err_fw:
	misc_deregister(&uc_dev);
err_priramw:
	cpcap_irq_free(data->cpcap, CPCAP_IRQ_UC_PRIRAMW);
err_priramr:
	cpcap_irq_free(data->cpcap, CPCAP_IRQ_UC_PRIRAMR);
err_ucreset:
	cpcap_irq_free(data->cpcap, CPCAP_IRQ_UCRESET);
err_primac:
	cpcap_irq_free(data->cpcap, CPCAP_IRQ_PRIMAC);
err_free:
	kfree(data);

	return retval;
}

static int __exit cpcap_uc_remove(struct platform_device *pdev)
{
	struct cpcap_uc_data *data = platform_get_drvdata(pdev);

	misc_deregister(&uc_dev);

	cpcap_irq_free(data->cpcap, CPCAP_IRQ_PRIMAC);
	cpcap_irq_free(data->cpcap, CPCAP_IRQ_UC_PRIRAMW);
	cpcap_irq_free(data->cpcap, CPCAP_IRQ_UC_PRIRAMR);
	cpcap_irq_free(data->cpcap, CPCAP_IRQ_UCRESET);

	kfree(data);
	return 0;
}


static struct platform_driver cpcap_uc_driver = {
	.probe		= cpcap_uc_probe,
	.remove		= __exit_p(cpcap_uc_remove),
	.driver		= {
		.name	= "cpcap_uc",
		.owner	= THIS_MODULE,
	},
};

static int __init cpcap_uc_init(void)
{
	return platform_driver_register(&cpcap_uc_driver);
}
subsys_initcall(cpcap_uc_init);

static void __exit cpcap_uc_exit(void)
{
	platform_driver_unregister(&cpcap_uc_driver);
}
module_exit(cpcap_uc_exit);

MODULE_ALIAS("platform:cpcap_uc");
MODULE_DESCRIPTION("CPCAP uC driver");
MODULE_AUTHOR("Motorola");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("cpcap/firmware_0_2x.fw");
MODULE_FIRMWARE("cpcap/firmware_1_2x.fw");

/*
 * Flash memory interface rev.5 driver for the Intel
 * Flash chips used on the NetWinder.
 *
 * 20/08/2000	RMK	use __ioremap to map flash into virtual memory
 *			make a few more places use "volatile"
 * 22/05/2001	RMK	- Lock read against write
 *			- merge printk level changes (with mods) from Alan Cox.
 *			- use *ppos as the file position, not file->f_pos.
 *			- fix check for out of range pos and r/w size
 *
 * Please note that we are tampering with the only flash chip in the
 * machine, which contains the bootup code.  We therefore have the
 * power to convert these machines into doorstops...
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>

#include <asm/hardware/dec21285.h>
#include <asm/io.h>
#include <asm/mach-types.h>
#include <asm/uaccess.h>

/*****************************************************************************/
#include <asm/nwflash.h>

#define	NWFLASH_VERSION "6.4"

static DEFINE_MUTEX(flash_mutex);
static void kick_open(void);
static int get_flash_id(void);
static int erase_block(int nBlock);
static int write_block(unsigned long p, const char __user *buf, int count);

#define KFLASH_SIZE	1024*1024	//1 Meg
#define KFLASH_SIZE4	4*1024*1024	//4 Meg
#define KFLASH_ID	0x89A6		//Intel flash
#define KFLASH_ID4	0xB0D4		//Intel flash 4Meg

static bool flashdebug;		//if set - we will display progress msgs

static int gbWriteEnable;
static int gbWriteBase64Enable;
static volatile unsigned char *FLASH_BASE;
static int gbFlashSize = KFLASH_SIZE;
static DEFINE_MUTEX(nwflash_mutex);

static int get_flash_id(void)
{
	volatile unsigned int c1, c2;

	/*
	 * try to get flash chip ID
	 */
	kick_open();
	c2 = inb(0x80);
	*(volatile unsigned char *) (FLASH_BASE + 0x8000) = 0x90;
	udelay(15);
	c1 = *(volatile unsigned char *) FLASH_BASE;
	c2 = inb(0x80);

	/*
	 * on 4 Meg flash the second byte is actually at offset 2...
	 */
	if (c1 == 0xB0)
		c2 = *(volatile unsigned char *) (FLASH_BASE + 2);
	else
		c2 = *(volatile unsigned char *) (FLASH_BASE + 1);

	c2 += (c1 << 8);

	/*
	 * set it back to read mode
	 */
	*(volatile unsigned char *) (FLASH_BASE + 0x8000) = 0xFF;

	if (c2 == KFLASH_ID4)
		gbFlashSize = KFLASH_SIZE4;

	return c2;
}

static long flash_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	mutex_lock(&flash_mutex);
	switch (cmd) {
	case CMD_WRITE_DISABLE:
		gbWriteBase64Enable = 0;
		gbWriteEnable = 0;
		break;

	case CMD_WRITE_ENABLE:
		gbWriteEnable = 1;
		break;

	case CMD_WRITE_BASE64K_ENABLE:
		gbWriteBase64Enable = 1;
		break;

	default:
		gbWriteBase64Enable = 0;
		gbWriteEnable = 0;
		mutex_unlock(&flash_mutex);
		return -EINVAL;
	}
	mutex_unlock(&flash_mutex);
	return 0;
}

static ssize_t flash_read(struct file *file, char __user *buf, size_t size,
			  loff_t *ppos)
{
	ssize_t ret;

	if (flashdebug)
		printk(KERN_DEBUG "flash_read: flash_read: offset=0x%llx, "
		       "buffer=%p, count=0x%zx.\n", *ppos, buf, size);
	/*
	 * We now lock against reads and writes. --rmk
	 */
	if (mutex_lock_interruptible(&nwflash_mutex))
		return -ERESTARTSYS;

	ret = simple_read_from_buffer(buf, size, ppos, (void *)FLASH_BASE, gbFlashSize);
	mutex_unlock(&nwflash_mutex);

	return ret;
}

static ssize_t flash_write(struct file *file, const char __user *buf,
			   size_t size, loff_t * ppos)
{
	unsigned long p = *ppos;
	unsigned int count = size;
	int written;
	int nBlock, temp, rc;
	int i, j;

	if (flashdebug)
		printk("flash_write: offset=0x%lX, buffer=0x%p, count=0x%X.\n",
		       p, buf, count);

	if (!gbWriteEnable)
		return -EINVAL;

	if (p < 64 * 1024 && (!gbWriteBase64Enable))
		return -EINVAL;

	/*
	 * check for out of range pos or count
	 */
	if (p >= gbFlashSize)
		return count ? -ENXIO : 0;

	if (count > gbFlashSize - p)
		count = gbFlashSize - p;
			
	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	/*
	 * We now lock against reads and writes. --rmk
	 */
	if (mutex_lock_interruptible(&nwflash_mutex))
		return -ERESTARTSYS;

	written = 0;

	nBlock = (int) p >> 16;	//block # of 64K bytes

	/*
	 * # of 64K blocks to erase and write
	 */
	temp = ((int) (p + count) >> 16) - nBlock + 1;

	/*
	 * write ends at exactly 64k boundary?
	 */
	if (((int) (p + count) & 0xFFFF) == 0)
		temp -= 1;

	if (flashdebug)
		printk(KERN_DEBUG "flash_write: writing %d block(s) "
			"starting at %d.\n", temp, nBlock);

	for (; temp; temp--, nBlock++) {
		if (flashdebug)
			printk(KERN_DEBUG "flash_write: erasing block %d.\n", nBlock);

		/*
		 * first we have to erase the block(s), where we will write...
		 */
		i = 0;
		j = 0;
	  RetryBlock:
		do {
			rc = erase_block(nBlock);
			i++;
		} while (rc && i < 10);

		if (rc) {
			printk(KERN_ERR "flash_write: erase error %x\n", rc);
			break;
		}
		if (flashdebug)
			printk(KERN_DEBUG "flash_write: writing offset %lX, "
			       "from buf %p, bytes left %X.\n", p, buf,
			       count - written);

		/*
		 * write_block will limit write to space left in this block
		 */
		rc = write_block(p, buf, count - written);
		j++;

		/*
		 * if somehow write verify failed? Can't happen??
		 */
		if (!rc) {
			/*
			 * retry up to 10 times
			 */
			if (j < 10)
				goto RetryBlock;
			else
				/*
				 * else quit with error...
				 */
				rc = -1;

		}
		if (rc < 0) {
			printk(KERN_ERR "flash_write: write error %X\n", rc);
			break;
		}
		p += rc;
		buf += rc;
		written += rc;
		*ppos += rc;

		if (flashdebug)
			printk(KERN_DEBUG "flash_write: written 0x%X bytes OK.\n", written);
	}

	mutex_unlock(&nwflash_mutex);

	return written;
}


/*
 * The memory devices use the full 32/64 bits of the offset, and so we cannot
 * check against negative addresses: they are ok. The return value is weird,
 * though, in that case (0).
 *
 * also note that seeking relative to the "end of file" isn't supported:
 * it has no meaning, so it returns -EINVAL.
 */
static loff_t flash_llseek(struct file *file, loff_t offset, int orig)
{
	loff_t ret;

	mutex_lock(&flash_mutex);
	if (flashdebug)
		printk(KERN_DEBUG "flash_llseek: offset=0x%X, orig=0x%X.\n",
		       (unsigned int) offset, orig);

	switch (orig) {
	case 0:
		if (offset < 0) {
			ret = -EINVAL;
			break;
		}

		if ((unsigned int) offset > gbFlashSize) {
			ret = -EINVAL;
			break;
		}

		file->f_pos = (unsigned int) offset;
		ret = file->f_pos;
		break;
	case 1:
		if ((file->f_pos + offset) > gbFlashSize) {
			ret = -EINVAL;
			break;
		}
		if ((file->f_pos + offset) < 0) {
			ret = -EINVAL;
			break;
		}
		file->f_pos += offset;
		ret = file->f_pos;
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&flash_mutex);
	return ret;
}


/*
 * assume that main Write routine did the parameter checking...
 * so just go ahead and erase, what requested!
 */

static int erase_block(int nBlock)
{
	volatile unsigned int c1;
	volatile unsigned char *pWritePtr;
	unsigned long timeout;
	int temp, temp1;

	/*
	 * reset footbridge to the correct offset 0 (...0..3)
	 */
	*CSR_ROMWRITEREG = 0;

	/*
	 * dummy ROM read
	 */
	c1 = *(volatile unsigned char *) (FLASH_BASE + 0x8000);

	kick_open();
	/*
	 * reset status if old errors
	 */
	*(volatile unsigned char *) (FLASH_BASE + 0x8000) = 0x50;

	/*
	 * erase a block...
	 * aim at the middle of a current block...
	 */
	pWritePtr = (unsigned char *) ((unsigned int) (FLASH_BASE + 0x8000 + (nBlock << 16)));
	/*
	 * dummy read
	 */
	c1 = *pWritePtr;

	kick_open();
	/*
	 * erase
	 */
	*(volatile unsigned char *) pWritePtr = 0x20;

	/*
	 * confirm
	 */
	*(volatile unsigned char *) pWritePtr = 0xD0;

	/*
	 * wait 10 ms
	 */
	msleep(10);

	/*
	 * wait while erasing in process (up to 10 sec)
	 */
	timeout = jiffies + 10 * HZ;
	c1 = 0;
	while (!(c1 & 0x80) && time_before(jiffies, timeout)) {
		msleep(10);
		/*
		 * read any address
		 */
		c1 = *(volatile unsigned char *) (pWritePtr);
		//              printk("Flash_erase: status=%X.\n",c1);
	}

	/*
	 * set flash for normal read access
	 */
	kick_open();
//      *(volatile unsigned char*)(FLASH_BASE+0x8000) = 0xFF;
	*(volatile unsigned char *) pWritePtr = 0xFF;	//back to normal operation

	/*
	 * check if erase errors were reported
	 */
	if (c1 & 0x20) {
		printk(KERN_ERR "flash_erase: err at %p\n", pWritePtr);

		/*
		 * reset error
		 */
		*(volatile unsigned char *) (FLASH_BASE + 0x8000) = 0x50;
		return -2;
	}

	/*
	 * just to make sure - verify if erased OK...
	 */
	msleep(10);

	pWritePtr = (unsigned char *) ((unsigned int) (FLASH_BASE + (nBlock << 16)));

	for (temp = 0; temp < 16 * 1024; temp++, pWritePtr += 4) {
		if ((temp1 = *(volatile unsigned int *) pWritePtr) != 0xFFFFFFFF) {
			printk(KERN_ERR "flash_erase: verify err at %p = %X\n",
			       pWritePtr, temp1);
			return -1;
		}
	}

	return 0;

}

/*
 * write_block will limit number of bytes written to the space in this block
 */
static int write_block(unsigned long p, const char __user *buf, int count)
{
	volatile unsigned int c1;
	volatile unsigned int c2;
	unsigned char *pWritePtr;
	unsigned int uAddress;
	unsigned int offset;
	unsigned long timeout;
	unsigned long timeout1;

	pWritePtr = (unsigned char *) ((unsigned int) (FLASH_BASE + p));

	/*
	 * check if write will end in this block....
	 */
	offset = p & 0xFFFF;

	if (offset + count > 0x10000)
		count = 0x10000 - offset;

	/*
	 * wait up to 30 sec for this block
	 */
	timeout = jiffies + 30 * HZ;

	for (offset = 0; offset < count; offset++, pWritePtr++) {
		uAddress = (unsigned int) pWritePtr;
		uAddress &= 0xFFFFFFFC;
		if (__get_user(c2, buf + offset))
			return -EFAULT;

	  WriteRetry:
	  	/*
	  	 * dummy read
	  	 */
		c1 = *(volatile unsigned char *) (FLASH_BASE + 0x8000);

		/*
		 * kick open the write gate
		 */
		kick_open();

		/*
		 * program footbridge to the correct offset...0..3
		 */
		*CSR_ROMWRITEREG = (unsigned int) pWritePtr & 3;

		/*
		 * write cmd
		 */
		*(volatile unsigned char *) (uAddress) = 0x40;

		/*
		 * data to write
		 */
		*(volatile unsigned char *) (uAddress) = c2;

		/*
		 * get status
		 */
		*(volatile unsigned char *) (FLASH_BASE + 0x10000) = 0x70;

		c1 = 0;

		/*
		 * wait up to 1 sec for this byte
		 */
		timeout1 = jiffies + 1 * HZ;

		/*
		 * while not ready...
		 */
		while (!(c1 & 0x80) && time_before(jiffies, timeout1))
			c1 = *(volatile unsigned char *) (FLASH_BASE + 0x8000);

		/*
		 * if timeout getting status
		 */
		if (time_after_eq(jiffies, timeout1)) {
			kick_open();
			/*
			 * reset err
			 */
			*(volatile unsigned char *) (FLASH_BASE + 0x8000) = 0x50;

			goto WriteRetry;
		}
		/*
		 * switch on read access, as a default flash operation mode
		 */
		kick_open();
		/*
		 * read access
		 */
		*(volatile unsigned char *) (FLASH_BASE + 0x8000) = 0xFF;

		/*
		 * if hardware reports an error writing, and not timeout - 
		 * reset the chip and retry
		 */
		if (c1 & 0x10) {
			kick_open();
			/*
			 * reset err
			 */
			*(volatile unsigned char *) (FLASH_BASE + 0x8000) = 0x50;

			/*
			 * before timeout?
			 */
			if (time_before(jiffies, timeout)) {
				if (flashdebug)
					printk(KERN_DEBUG "write_block: Retrying write at 0x%X)n",
					       pWritePtr - FLASH_BASE);

				/*
				 * wait couple ms
				 */
				msleep(10);

				goto WriteRetry;
			} else {
				printk(KERN_ERR "write_block: timeout at 0x%X\n",
				       pWritePtr - FLASH_BASE);
				/*
				 * return error -2
				 */
				return -2;

			}
		}
	}

	msleep(10);

	pWritePtr = (unsigned char *) ((unsigned int) (FLASH_BASE + p));

	for (offset = 0; offset < count; offset++) {
		char c, c1;
		if (__get_user(c, buf))
			return -EFAULT;
		buf++;
		if ((c1 = *pWritePtr++) != c) {
			printk(KERN_ERR "write_block: verify error at 0x%X (%02X!=%02X)\n",
			       pWritePtr - FLASH_BASE, c1, c);
			return 0;
		}
	}

	return count;
}


static void kick_open(void)
{
	unsigned long flags;

	/*
	 * we want to write a bit pattern XXX1 to Xilinx to enable
	 * the write gate, which will be open for about the next 2ms.
	 */
	spin_lock_irqsave(&nw_gpio_lock, flags);
	nw_cpld_modify(CPLD_FLASH_WR_ENABLE, CPLD_FLASH_WR_ENABLE);
	spin_unlock_irqrestore(&nw_gpio_lock, flags);

	/*
	 * let the ISA bus to catch on...
	 */
	udelay(25);
}

static const struct file_operations flash_fops =
{
	.owner		= THIS_MODULE,
	.llseek		= flash_llseek,
	.read		= flash_read,
	.write		= flash_write,
	.unlocked_ioctl	= flash_ioctl,
};

static struct miscdevice flash_miscdev =
{
	FLASH_MINOR,
	"nwflash",
	&flash_fops
};

static int __init nwflash_init(void)
{
	int ret = -ENODEV;

	if (machine_is_netwinder()) {
		int id;

		FLASH_BASE = ioremap(DC21285_FLASH, KFLASH_SIZE4);
		if (!FLASH_BASE)
			goto out;

		id = get_flash_id();
		if ((id != KFLASH_ID) && (id != KFLASH_ID4)) {
			ret = -ENXIO;
			iounmap((void *)FLASH_BASE);
			printk("Flash: incorrect ID 0x%04X.\n", id);
			goto out;
		}

		printk("Flash ROM driver v.%s, flash device ID 0x%04X, size %d Mb.\n",
		       NWFLASH_VERSION, id, gbFlashSize / (1024 * 1024));

		ret = misc_register(&flash_miscdev);
		if (ret < 0) {
			iounmap((void *)FLASH_BASE);
		}
	}
out:
	return ret;
}

static void __exit nwflash_exit(void)
{
	misc_deregister(&flash_miscdev);
	iounmap((void *)FLASH_BASE);
}

MODULE_LICENSE("GPL");

module_param(flashdebug, bool, 0644);

module_init(nwflash_init);
module_exit(nwflash_exit);

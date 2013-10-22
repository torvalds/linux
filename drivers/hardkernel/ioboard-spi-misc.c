//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  I2C IOBOARD_SPI(MISC) driver
//  2013.07.17
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/spi/spi.h>

//[*]--------------------------------------------------------------------------------------------------[*]
#include "ioboard-spi.h"

//[*]--------------------------------------------------------------------------------------------------[*]
//
// function prototype
//
//[*]--------------------------------------------------------------------------------------------------[*]
static 	int 	ioboard_spi_misc_open	(struct inode *inode, struct file *file);
static 	long 	ioboard_spi_misc_ioctl	(struct file *file, unsigned int cmd, unsigned long arg);
		void 	ioboard_spi_misc_remove	(struct device *dev);
		int		ioboard_spi_misc_probe	(struct spi_device *spi);

//[*]--------------------------------------------------------------------------------------------------[*]
static const struct file_operations 	ioboard_spi_misc_fops = {
	.owner		    = THIS_MODULE,
	.open		    = ioboard_spi_misc_open,
	.unlocked_ioctl	= ioboard_spi_misc_ioctl,
};

//[*]--------------------------------------------------------------------------------------------------[*]
static long 	ioboard_spi_misc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct ioboard_spi          *spi    = (struct ioboard_spi *)file->private_data;
    struct ioboard_spi_iocreg   *iocreg = (struct ioboard_spi_iocreg *)arg;

	switch (cmd) {
        case    IOBOARD_IOCGREG   :
            if(iocreg->cmd == IOBOARD_CMD_SPI_READ)    {
                if(ioboard_spi_read(spi->spi, iocreg->addr, &iocreg->rwdata[0], iocreg->size)) {
                    printk("%s : IOBOARD_IOCGREG error!\n", __func__);  return  -1;
                }
            }
            break;
        case    IOBOARD_IOCSREG   :
            if(iocreg->cmd == IOBOARD_CMD_SPI_WRITE)    {
                if(ioboard_spi_write(spi->spi, iocreg->addr, &iocreg->rwdata[0], iocreg->size)) {
                    printk("%s : IOBOARD_IOCSREG error!\n", __func__);  return  -1;
                }
            }
            break;
        case    IOBOARD_IOCGSTATUS:
            if(iocreg->cmd == IOBOARD_CMD_SPI_ERASE)    {
                if(ioboard_spi_erase(spi->spi, iocreg->addr, iocreg->size)) {
                    printk("%s : IOBOARD_IOCSREG error!\n", __func__);  return  -1;
                }
            }
            break;
		default :
		    printk("%s : unknown message!!\n", __func__);   return  -1;
	}
	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
struct  spi_device  *gSPI;

static int 	ioboard_spi_misc_open(struct inode *inode, struct file *file)
{
    struct  ioboard_spi *spi = dev_get_drvdata(&gSPI->dev);
    
    file->private_data = spi;
    
	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
int		ioboard_spi_misc_probe(struct spi_device *spi)
{
	int rc;
	
    struct ioboard_spi  *ioboard_spi = dev_get_drvdata(&spi->dev);

	if(!(ioboard_spi->misc = kzalloc(sizeof(struct miscdevice), GFP_KERNEL)))	{
		printk("ioboard-spi misc struct malloc error!\n");
		return	-ENOMEM;
	}
    ioboard_spi->misc->minor = MISC_DYNAMIC_MINOR;
    ioboard_spi->misc->name = "ioboard-spi-misc";
    ioboard_spi->misc->fops = &ioboard_spi_misc_fops;
    
    gSPI = spi;
    
	if((rc = misc_register(ioboard_spi->misc)) < 0)	{
		printk("%s : ioboard_spi misc register fail!\n", __func__);		
		kfree(ioboard_spi->misc);
		return	rc;
	}

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
void 	ioboard_spi_misc_remove(struct device *dev)
{
	struct ioboard_spi  *ioboard_spi = dev_get_drvdata(dev);

	misc_deregister(ioboard_spi->misc);

    kfree(ioboard_spi->misc);
}

//[*]--------------------------------------------------------------------------------------------------[*]
MODULE_AUTHOR("HardKernel Co., Ltd.");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ioboard_spi misc Driver");

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]

/*
 * FILE NAME ite_gpio.c
 *
 * BRIEF MODULE DESCRIPTION
 *  API for ITE GPIO device.
 *  Driver for ITE GPIO device.
 *
 *  Author: MontaVista Software, Inc.  <source@mvista.com>
 *          Hai-Pao Fan <haipao@mvista.com>
 *
 * Copyright 2001 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE	LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <asm/uaccess.h>
#include <asm/addrspace.h>
#include <asm/it8172/it8172_int.h>
#include <linux/sched.h>
#include <linux/ite_gpio.h>

#define ite_gpio_base 0x14013800

#define	ITE_GPADR	(*(volatile __u8 *)(0x14013800 + KSEG1))
#define	ITE_GPBDR	(*(volatile __u8 *)(0x14013808 + KSEG1))
#define	ITE_GPCDR	(*(volatile __u8 *)(0x14013810 + KSEG1))
#define	ITE_GPACR	(*(volatile __u16 *)(0x14013802 + KSEG1))
#define	ITE_GPBCR	(*(volatile __u16 *)(0x1401380a + KSEG1))
#define	ITE_GPCCR	(*(volatile __u16 *)(0x14013812 + KSEG1))
#define ITE_GPAICR	(*(volatile __u16 *)(0x14013804 + KSEG1))
#define	ITE_GPBICR	(*(volatile __u16 *)(0x1401380c + KSEG1))
#define	ITE_GPCICR	(*(volatile __u16 *)(0x14013814 + KSEG1))
#define	ITE_GPAISR	(*(volatile __u8 *)(0x14013806 + KSEG1))
#define	ITE_GPBISR	(*(volatile __u8 *)(0x1401380e + KSEG1))
#define	ITE_GPCISR	(*(volatile __u8 *)(0x14013816 + KSEG1))
#define	ITE_GCR		(*(volatile __u8 *)(0x14013818 + KSEG1))

#define MAX_GPIO_LINE		21
static int ite_gpio_irq=IT8172_GPIO_IRQ;

static long ite_irq_counter[MAX_GPIO_LINE];
wait_queue_head_t ite_gpio_wait[MAX_GPIO_LINE];
static int ite_gpio_irq_pending[MAX_GPIO_LINE];

static int ite_gpio_debug=0;
#define DEB(x)  if (ite_gpio_debug>=1) x

int ite_gpio_in(__u32 device, __u32 mask, volatile __u32 *data)
{
	DEB(printk("ite_gpio_in mask=0x%x\n",mask)); 

	switch (device) {
	case ITE_GPIO_PORTA:
		ITE_GPACR = (__u16)mask;	/* 0xffff */
		*data = ITE_GPADR;
		break;
	case ITE_GPIO_PORTB:
		ITE_GPBCR = (__u16)mask;	/* 0xffff */
		*data = ITE_GPBDR;
		break;
	case ITE_GPIO_PORTC:
		ITE_GPCCR = (__u16)mask;	/* 0x03ff */
		*data = ITE_GPCDR;
		break;
	default:
		return -EFAULT;
	}

	return 0;
}


int ite_gpio_out(__u32 device, __u32 mask, __u32 data)
{
	switch (device) {
	case ITE_GPIO_PORTA:
		ITE_GPACR = (__u16)mask;	/* 0x5555 */
		ITE_GPADR = (__u8)data;
		break;
	case ITE_GPIO_PORTB:
		ITE_GPBCR = (__u16)mask;	/* 0x5555 */
		ITE_GPBDR = (__u8)data;
		break;
	case ITE_GPIO_PORTC:
		ITE_GPCCR = (__u16)mask;	/* 0x0155 */
		ITE_GPCDR = (__u8)data;
		break;
	default:
		return -EFAULT;
	}

	return 0;
}

int ite_gpio_int_ctrl(__u32 device, __u32 mask, __u32 data)
{
	switch (device) {
	case ITE_GPIO_PORTA:
		ITE_GPAICR = (ITE_GPAICR & ~mask) | (data & mask);
		break;
	case ITE_GPIO_PORTB:
		ITE_GPBICR = (ITE_GPBICR & ~mask) | (data & mask);
		break;
	case ITE_GPIO_PORTC:
		ITE_GPCICR = (ITE_GPCICR & ~mask) | (data & mask);
		break;
	default:
		return -EFAULT;
	}

	return 0;
}

int ite_gpio_in_status(__u32 device, __u32 mask, volatile __u32 *data)
{
	int ret=-1;

	if ((MAX_GPIO_LINE > *data) && (*data >= 0)) 
		ret=ite_gpio_irq_pending[*data];
 
	DEB(printk("ite_gpio_in_status %d ret=%d\n",*data, ret));

	switch (device) {
	case ITE_GPIO_PORTA:
		*data = ITE_GPAISR & mask;
		break;
	case ITE_GPIO_PORTB:
		*data = ITE_GPBISR & mask;
		break;
	case ITE_GPIO_PORTC:
		*data = ITE_GPCISR & mask;
		break;
	default:
		return -EFAULT;
	}

	return ret;
}

int ite_gpio_out_status(__u32 device, __u32 mask, __u32 data)
{
	switch (device) {
	case ITE_GPIO_PORTA:
		ITE_GPAISR = (ITE_GPAISR & ~mask) | (data & mask);
		break;
	case ITE_GPIO_PORTB:
		ITE_GPBISR = (ITE_GPBISR & ~mask) | (data & mask);
		break;
	case ITE_GPIO_PORTC:
		ITE_GPCISR = (ITE_GPCISR & ~mask) | (data & mask);
		break;
	default:
		return -EFAULT;
	}

	return 0;
}

int ite_gpio_gen_ctrl(__u32 device, __u32 mask, __u32 data)
{
	ITE_GCR = (ITE_GCR & ~mask) | (data & mask);

	return 0;
}

int ite_gpio_int_wait (__u32 device, __u32 mask, __u32 data)
{
	int i,line=0, ret=0;
	unsigned long flags;

	switch (device) {
	case ITE_GPIO_PORTA:
		line = data & mask;
		break;
	case ITE_GPIO_PORTB:
		line = (data & mask) <<8;
		break;
	case ITE_GPIO_PORTC:
		line = (data & mask) <<16;
		break;
	}
	for (i=MAX_GPIO_LINE-1; i >= 0; i--) {
		if ( (line) & (1 << i))
			break;
	}

	DEB(printk("wait device=0x%d mask=0x%x data=0x%x index %d\n", 
		device, mask, data, i));

	if (line & ~(1<<i))
		return -EFAULT;

	if (ite_gpio_irq_pending[i]==1)
		return -EFAULT;

	save_flags (flags);
	cli();
	ite_gpio_irq_pending[i] = 1;
	ret = interruptible_sleep_on_timeout(&ite_gpio_wait[i], 3*HZ);
	restore_flags (flags);
	ite_gpio_irq_pending[i] = 0;

	return ret;
}

EXPORT_SYMBOL(ite_gpio_in);
EXPORT_SYMBOL(ite_gpio_out);
EXPORT_SYMBOL(ite_gpio_int_ctrl);
EXPORT_SYMBOL(ite_gpio_in_status);
EXPORT_SYMBOL(ite_gpio_out_status);
EXPORT_SYMBOL(ite_gpio_gen_ctrl);
EXPORT_SYMBOL(ite_gpio_int_wait);

static int ite_gpio_open(struct inode *inode, struct file *file)
{
	return 0;
}


static int ite_gpio_release(struct inode *inode, struct file *file)
{
	return 0;
}


static int ite_gpio_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	static struct ite_gpio_ioctl_data ioctl_data;

	if (copy_from_user(&ioctl_data, (struct ite_gpio_ioctl_data *)arg,
			sizeof(ioctl_data)))
		return -EFAULT;
	if ((ioctl_data.device < ITE_GPIO_PORTA) ||
			(ioctl_data.device > ITE_GPIO_PORTC) )
				return -EFAULT;

	switch(cmd) {
		case ITE_GPIO_IN:
			if (ite_gpio_in(ioctl_data.device, ioctl_data.mask,
					   &ioctl_data.data))
				return -EFAULT;

			if (copy_to_user((struct ite_gpio_ioctl_data *)arg,
					 &ioctl_data, sizeof(ioctl_data)))
				return -EFAULT;
			break;

		case ITE_GPIO_OUT:
			return ite_gpio_out(ioctl_data.device,
					ioctl_data.mask, ioctl_data.data);
			break;

		case ITE_GPIO_INT_CTRL:
			return ite_gpio_int_ctrl(ioctl_data.device,
					ioctl_data.mask, ioctl_data.data);
			break;

		case ITE_GPIO_IN_STATUS:
			if (ite_gpio_in_status(ioctl_data.device, ioctl_data.mask,
					&ioctl_data.data))
				return -EFAULT;
			if (copy_to_user((struct ite_gpio_ioctl_data *)arg,
					&ioctl_data, sizeof(ioctl_data))) 
				return -EFAULT;
			break;

		case ITE_GPIO_OUT_STATUS:
			return ite_gpio_out_status(ioctl_data.device,
					ioctl_data.mask, ioctl_data.data);
			break;

		case ITE_GPIO_GEN_CTRL:
			return ite_gpio_gen_ctrl(ioctl_data.device,
					ioctl_data.mask, ioctl_data.data);
			break;

		case ITE_GPIO_INT_WAIT:
			return ite_gpio_int_wait(ioctl_data.device,
					ioctl_data.mask, ioctl_data.data);
			break;

		default:
			return -ENOIOCTLCMD;

	}

	return 0;
}

static void ite_gpio_irq_handler(int this_irq, void *dev_id,
	struct pt_regs *regs)
{
	int i,line;

	line = ITE_GPCISR & 0x1f;
	for (i=4; i >=0; i--) {
		if ( line & (1 << i)) { 
			++ite_irq_counter[i+16];
			ite_gpio_irq_pending[i+16] = 2;
			wake_up_interruptible(&ite_gpio_wait[i+16]);

DEB(printk("interrupt 0x%x %d\n", &ite_gpio_wait[i+16], i+16));

			ITE_GPCISR = ITE_GPCISR & (1<<i);
			return;
		}
	}
	line = ITE_GPBISR;
	for (i=7; i >= 0; i--) {
		if ( line & (1 << i)) {
			++ite_irq_counter[i+8];
			ite_gpio_irq_pending[i+8] = 2;
			wake_up_interruptible(&ite_gpio_wait[i+8]);

DEB(printk("interrupt 0x%x %d\n",ITE_GPBISR, i+8));

			ITE_GPBISR = ITE_GPBISR & (1<<i);
			return;
		}
	}
	line = ITE_GPAISR;
	for (i=7; i >= 0; i--) {
		if ( line & (1 << i)) {
			++ite_irq_counter[i];
			ite_gpio_irq_pending[i] = 2;
			wake_up_interruptible(&ite_gpio_wait[i]);

DEB(printk("interrupt 0x%x %d\n",ITE_GPAISR, i));

			ITE_GPAISR = ITE_GPAISR & (1<<i);
			return;
		}
	}
}

static struct file_operations ite_gpio_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= ite_gpio_ioctl,
	.open		= ite_gpio_open,
	.release	= ite_gpio_release,
};

static struct miscdevice ite_gpio_miscdev = {
	MISC_DYNAMIC_MINOR,
	"ite_gpio",
	&ite_gpio_fops
};

int __init ite_gpio_init(void)
{
	int i;

	if (misc_register(&ite_gpio_miscdev))
		return -ENODEV;

	if (!request_region(ite_gpio_base, 0x1c, "ITE GPIO"))
	{
		misc_deregister(&ite_gpio_miscdev);
		return -EIO;
	}

	/* initialize registers */
        ITE_GPACR = 0xffff;
        ITE_GPBCR = 0xffff;
        ITE_GPCCR = 0xffff;
        ITE_GPAICR = 0x00ff;
        ITE_GPBICR = 0x00ff;
        ITE_GPCICR = 0x00ff;
        ITE_GCR = 0;
	
	for (i = 0; i < MAX_GPIO_LINE; i++) {
		ite_gpio_irq_pending[i]=0;	
		init_waitqueue_head(&ite_gpio_wait[i]);
	}

	if (request_irq(ite_gpio_irq, ite_gpio_irq_handler, SA_SHIRQ, "gpio", 0) < 0) {
		misc_deregister(&ite_gpio_miscdev);
		release_region(ite_gpio_base, 0x1c);
		return 0;
	}

	printk("GPIO at 0x%x (irq = %d)\n", ite_gpio_base, ite_gpio_irq);

	return 0;
}	

static void __exit ite_gpio_exit(void)
{
	misc_deregister(&ite_gpio_miscdev);
}

module_init(ite_gpio_init);
module_exit(ite_gpio_exit);

MODULE_LICENSE("GPL");

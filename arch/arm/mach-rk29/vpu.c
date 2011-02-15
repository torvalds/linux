/* arch/arm/mach-rk29/vpu.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifdef CONFIG_RK29_VPU_DEBUG
#define DEBUG
#define pr_fmt(fmt) "VPU: %s: " fmt, __func__
#else
#define pr_fmt(fmt) "VPU: " fmt
#endif

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/platform_device.h>

#include <asm/uaccess.h>

#include <mach/irqs.h>
#include <mach/vpu.h>
#include <mach/rk29_iomap.h>
#include <mach/pmu.h>
#include <mach/cru.h>


#define DEC_INTERRUPT_REGISTER     1
#define PP_INTERRUPT_REGISTER      60
#define ENC_INTERRUPT_REGISTER     1

#define DEC_INTERRUPT_BIT            0x100
#define PP_INTERRUPT_BIT             0x100
#define ENC_INTERRUPT_BIT            0x1

#define DEC_IO_SIZE                 ((100 + 1) * 4)	/* bytes */
#define ENC_IO_SIZE                 (96 * 4)	/* bytes */

static const u16 dec_hw_ids[] = { 0x8190, 0x8170, 0x9170, 0x9190, 0x6731 };
static const u16 enc_hw_ids[] = { 0x6280, 0x7280, 0x8270 };

struct vpu_device {
	unsigned long	iobaseaddr;
	unsigned int	iosize;
	volatile u32	*hwregs;
	unsigned int	irq;
};

static struct vpu_device dec_dev;
static struct vpu_device pp_dev;
static struct vpu_device enc_dev;

struct vpu_client {
	atomic_t		dec_event;
	atomic_t		enc_event;
	struct fasync_struct	*async_queue;
	wait_queue_head_t	wait;
	struct file		*filp;	/* for /proc/vpu */
	bool			enabled;
};
static struct vpu_client client;

static struct clk *aclk_vepu;
static struct clk *hclk_vepu;
static struct clk *aclk_ddr_vepu;
static struct clk *hclk_cpu_vcodec;

static void vpu_release_io(void);

static void vpu_get_clk(void)
{
	aclk_vepu = clk_get(NULL, "aclk_vepu");
	hclk_vepu = clk_get(NULL, "hclk_vepu");
	aclk_ddr_vepu = clk_get(NULL, "aclk_ddr_vepu");
	hclk_cpu_vcodec = clk_get(NULL, "hclk_cpu_vcodec");
}

static void vpu_put_clk(void)
{
	clk_put(aclk_vepu);
	clk_put(hclk_vepu);
	clk_put(aclk_ddr_vepu);
	clk_put(hclk_cpu_vcodec);
}

static void vpu_power_on(void)
{
	pr_debug("power on\n");
	if (client.enabled)
		return;
#if 0
	pr_debug("power domain on\n");
	pmu_set_power_domain(PD_VCODEC, true);
	udelay(10);
#endif
	clk_enable(aclk_vepu);
	clk_enable(hclk_vepu);
	clk_enable(aclk_ddr_vepu);
	clk_enable(hclk_cpu_vcodec);
#if 0
	udelay(10);
	cru_set_soft_reset(SOFT_RST_CPU_VODEC_A2A_AHB, true);
	cru_set_soft_reset(SOFT_RST_VCODEC_AHB_BUS, true);
	cru_set_soft_reset(SOFT_RST_VCODEC_AXI_BUS, true);
	cru_set_soft_reset(SOFT_RST_DDR_VCODEC_PORT, true);
	udelay(10);
	cru_set_soft_reset(SOFT_RST_CPU_VODEC_A2A_AHB, false);
	cru_set_soft_reset(SOFT_RST_VCODEC_AHB_BUS, false);
	cru_set_soft_reset(SOFT_RST_VCODEC_AXI_BUS, false);
	cru_set_soft_reset(SOFT_RST_DDR_VCODEC_PORT, false);
#endif
	client.enabled = true;
}

static void vpu_power_off(void)
{
	pr_debug("power off\n");
	if (!client.enabled)
		return;
	clk_disable(hclk_cpu_vcodec);
	clk_disable(aclk_ddr_vepu);
	clk_disable(hclk_vepu);
	clk_disable(aclk_vepu);
#if 0
	pr_debug("power domain off\n");
	pmu_set_power_domain(PD_VCODEC, false);
#endif
	client.enabled = false;
}

static long vpu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	pr_debug("ioctl cmd 0x%08x\n", cmd);

	switch (cmd) {
	case VPU_IOC_POWER_ON: {
		vpu_power_on();
		break;
	}
	case VPU_IOC_POWER_OFF: {
		vpu_power_off();
		break;
	}
	default:
		return -ENOTTY;
	}

	return 0;
}

static int vpu_open(struct inode *inode, struct file *filp)
{
	if (client.filp)
		return -EBUSY;

	client.filp = filp;
	vpu_power_on();

	pr_debug("dev opened\n");
	return nonseekable_open(inode, filp);
}

static int vpu_fasync(int fd, struct file *filp, int mode)
{
	return fasync_helper(fd, filp, mode, &client.async_queue);
}

static int vpu_release(struct inode *inode, struct file *filp)
{
	/* remove this filp from the asynchronusly notified filp's */
	vpu_fasync(-1, filp, 0);

	client.async_queue = NULL;
	client.filp = NULL;

	vpu_power_off();

	pr_debug("dev closed\n");
	return 0;
}

static int vpu_check_hw_id(struct vpu_device * dev, const u16 *hwids, size_t num)
{
	u32 hwid = readl(dev->hwregs);
	pr_info("HW ID = 0x%08x\n", hwid);

	hwid = (hwid >> 16) & 0xFFFF;	/* product version only */

	while (num--) {
		if (hwid == hwids[num]) {
			pr_info("Compatible HW found at 0x%08lx\n", dev->iobaseaddr);
			return 1;
		}
	}

	pr_info("No Compatible HW found at 0x%08lx\n", dev->iobaseaddr);
	return 0;
}

static int vpu_reserve_io(void)
{
	if (!request_mem_region(dec_dev.iobaseaddr, dec_dev.iosize, "hx170dec")) {
		pr_info("failed to reserve dec HW regs\n");
		return -EBUSY;
	}

	dec_dev.hwregs =
	    (volatile u32 *)ioremap_nocache(dec_dev.iobaseaddr, dec_dev.iosize);

	if (dec_dev.hwregs == NULL) {
		pr_info("failed to ioremap dec HW regs\n");
		goto err;
	}

	/* check for correct HW */
	if (!vpu_check_hw_id(&dec_dev, dec_hw_ids, ARRAY_SIZE(dec_hw_ids))) {
		goto err;
	}

	if (!request_mem_region(enc_dev.iobaseaddr, enc_dev.iosize, "hx280enc")) {
		pr_info("failed to reserve enc HW regs\n");
		goto err;
	}

	enc_dev.hwregs =
	    (volatile u32 *)ioremap_nocache(enc_dev.iobaseaddr, enc_dev.iosize);

	if (enc_dev.hwregs == NULL) {
		pr_info("failed to ioremap enc HW regs\n");
		goto err;
	}

	/* check for correct HW */
	if (!vpu_check_hw_id(&enc_dev, enc_hw_ids, ARRAY_SIZE(enc_hw_ids))) {
		goto err;
	}
	return 0;

err:
	vpu_release_io();
	return -EBUSY;
}

static void vpu_release_io(void)
{
	if (dec_dev.hwregs)
		iounmap((void *)dec_dev.hwregs);
	release_mem_region(dec_dev.iobaseaddr, dec_dev.iosize);

	if (enc_dev.hwregs)
		iounmap((void *)enc_dev.hwregs);
	release_mem_region(enc_dev.iobaseaddr, enc_dev.iosize);
}

static void vpu_event_notify(void)
{
	wake_up_interruptible(&client.wait);
	if (client.async_queue)
		kill_fasync(&client.async_queue, SIGIO, POLL_IN);
}

static irqreturn_t hx170dec_isr(int irq, void *dev_id)
{
	struct vpu_device *dev = (struct vpu_device *) dev_id;
	u32 irq_status_dec;
	u32 irq_status_pp;
	u32 event = VPU_IRQ_EVENT_DEC_BIT;

	/* interrupt status register read */
	irq_status_dec = readl(dev->hwregs + DEC_INTERRUPT_REGISTER);
	irq_status_pp = readl(dev->hwregs + PP_INTERRUPT_REGISTER);

	if (irq_status_dec & DEC_INTERRUPT_BIT) {
		/* clear dec IRQ */
		writel(irq_status_dec & (~DEC_INTERRUPT_BIT),
				dev->hwregs + DEC_INTERRUPT_REGISTER);

		event |= VPU_IRQ_EVENT_DEC_IRQ_BIT;

		pr_debug("DEC IRQ received!\n");
	}

	if (irq_status_pp & PP_INTERRUPT_BIT) {
		/* clear pp IRQ */
		writel(irq_status_pp & (~DEC_INTERRUPT_BIT),
				dev->hwregs + PP_INTERRUPT_REGISTER);

		event |= VPU_IRQ_EVENT_PP_IRQ_BIT;

		pr_debug("PP IRQ received!\n");
	}

	atomic_set(&client.dec_event, event);
	vpu_event_notify();

	return IRQ_HANDLED;
}

static irqreturn_t hx280enc_isr(int irq, void *dev_id)
{
	struct vpu_device *dev = (struct vpu_device *) dev_id;
	u32 irq_status;
	u32 event = VPU_IRQ_EVENT_ENC_BIT;

	irq_status = readl(dev->hwregs + ENC_INTERRUPT_REGISTER);

	if (likely(irq_status & ENC_INTERRUPT_BIT)) {
		/* clear enc IRQ */
		writel(irq_status & (~ENC_INTERRUPT_BIT),
				dev->hwregs + ENC_INTERRUPT_REGISTER);

		event |= VPU_IRQ_EVENT_ENC_IRQ_BIT;

		pr_debug("ENC IRQ received!\n");
	}

	atomic_set(&client.enc_event, event);
	vpu_event_notify();

	return IRQ_HANDLED;
}

static int vpu_mmap(struct file *fp, struct vm_area_struct *vm)
{
	unsigned long pfn;

	/* Only support the simple cases where we map in a register page. */
	if (((vm->vm_end - vm->vm_start) > RK29_VCODEC_SIZE) || vm->vm_pgoff)
		return -EINVAL;

	vm->vm_flags |= VM_IO | VM_RESERVED;
	vm->vm_page_prot = pgprot_noncached(vm->vm_page_prot);
	pfn = RK29_VCODEC_PHYS >> PAGE_SHIFT;
	pr_debug("size = 0x%x, page no. = 0x%x\n",
			(int)(vm->vm_end - vm->vm_start), (int)pfn);
	return remap_pfn_range(vm, vm->vm_start, pfn, vm->vm_end - vm->vm_start,
			vm->vm_page_prot) ? -EAGAIN : 0;
}

static unsigned int vpu_poll(struct file *filep, poll_table *wait)
{
	poll_wait(filep, &client.wait, wait);

	if (atomic_read(&client.dec_event) || atomic_read(&client.enc_event))
		return POLLIN | POLLRDNORM;
	return 0;
}

static ssize_t vpu_read(struct file *filep, char __user *buf,
			size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	ssize_t retval;
	u32 irq_event;

	if (count != sizeof(u32))
		return -EINVAL;

	add_wait_queue(&client.wait, &wait);

	do {
		set_current_state(TASK_INTERRUPTIBLE);

		irq_event = atomic_xchg(&client.dec_event, 0) | atomic_xchg(&client.enc_event, 0);

		if (irq_event) {
			if (copy_to_user(buf, &irq_event, count))
				retval = -EFAULT;
			else
				retval = count;
			break;
		}

		if (filep->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	} while (1);

	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&client.wait, &wait);

	return retval;
}

static const struct file_operations vpu_fops = {
	.read		= vpu_read,
	.poll		= vpu_poll,
	.unlocked_ioctl	= vpu_ioctl,
	.mmap		= vpu_mmap,
	.open		= vpu_open,
	.release	= vpu_release,
	.fasync		= vpu_fasync,
};

static struct miscdevice vpu_misc_device = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "vpu",
	.fops		= &vpu_fops,
};

#ifdef CONFIG_PM
static int vpu_suspend(struct platform_device *pdev, pm_message_t state)
{
	bool enabled = client.enabled;
	vpu_power_off();
	client.enabled = enabled;
	return 0;
}

static int vpu_resume(struct platform_device *pdev)
{
	if (client.enabled) {
		client.enabled = false;
		vpu_power_on();
	}
	return 0;
}

static struct platform_device vpu_pm_device = {
	.name          = "vpu",
	.id            = -1,
};

static struct platform_driver vpu_pm_driver = {
	.driver    = {
		.name  = "vpu",
		.owner = THIS_MODULE,
	},
	.suspend   = vpu_suspend,
	.resume    = vpu_resume,
};
#endif

static int __init vpu_init(void)
{
	int ret;

	pr_debug("baseaddr = 0x%08x vdpu irq = %d vepu irq = %d\n", RK29_VCODEC_PHYS, IRQ_VDPU, IRQ_VEPU);

	dec_dev.iobaseaddr = RK29_VCODEC_PHYS + 0x200;
	dec_dev.iosize = DEC_IO_SIZE;
	dec_dev.irq = IRQ_VDPU;

	enc_dev.iobaseaddr = RK29_VCODEC_PHYS;
	enc_dev.iosize = ENC_IO_SIZE;
	enc_dev.irq = IRQ_VEPU;

	vpu_get_clk();
	vpu_power_on();

	ret = vpu_reserve_io();
	if (ret < 0) {
		goto err_reserve_io;
	}
	pp_dev = dec_dev;

	init_waitqueue_head(&client.wait);
	atomic_set(&client.dec_event, 0);
	atomic_set(&client.enc_event, 0);

	/* get the IRQ line */
	ret = request_irq(IRQ_VDPU, hx170dec_isr, 0, "hx170dec", (void *)&dec_dev);
	if (ret != 0) {
		pr_err("can't request vdpu irq %d\n", IRQ_VDPU);
		goto err_req_vdpu_irq;
	}

	ret = request_irq(IRQ_VEPU, hx280enc_isr, 0, "hx280enc", (void *)&enc_dev);
	if (ret != 0) {
		pr_err("can't request vepu irq %d\n", IRQ_VEPU);
		goto err_req_vepu_irq;
	}

	ret = misc_register(&vpu_misc_device);
	if (ret) {
		pr_err("misc_register failed\n");
		goto err_register;
	}

	vpu_power_off();

#ifdef CONFIG_PM
	platform_device_register(&vpu_pm_device);
	platform_driver_probe(&vpu_pm_driver, NULL);
#endif
	pr_info("init success\n");

	return 0;

err_register:
	free_irq(IRQ_VEPU, (void *)&enc_dev);
err_req_vepu_irq:
	free_irq(IRQ_VDPU, (void *)&dec_dev);
err_req_vdpu_irq:
	vpu_release_io();
err_reserve_io:
	vpu_power_off();
	vpu_put_clk();
	pr_info("init failed\n");
	return ret;
}

static void __exit vpu_exit(void)
{
#ifdef CONFIG_PM
	platform_device_unregister(&vpu_pm_device);
	platform_driver_unregister(&vpu_pm_driver);
#endif

	vpu_power_on();

	misc_deregister(&vpu_misc_device);
	free_irq(IRQ_VEPU, (void *)&enc_dev);
	free_irq(IRQ_VDPU, (void *)&dec_dev);
	vpu_release_io();

	vpu_power_off();
	vpu_put_clk();
}

module_init(vpu_init);
module_exit(vpu_exit);
MODULE_LICENSE("GPL");

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static int proc_vpu_show(struct seq_file *s, void *v)
{
	unsigned int i, n;
	s32 irq_event = atomic_read(&client.dec_event) | atomic_read(&client.enc_event);

	seq_printf(s, client.filp ? "Opened\n" : "Closed\n");
	seq_printf(s, "irq_event: 0x%08x (%s%s%s%s%s)\n", irq_event,
		   irq_event & VPU_IRQ_EVENT_DEC_BIT ? "DEC " : "",
		   irq_event & VPU_IRQ_EVENT_DEC_IRQ_BIT ? "DEC_IRQ " : "",
		   irq_event & VPU_IRQ_EVENT_PP_IRQ_BIT ? "PP_IRQ " : "",
		   irq_event & VPU_IRQ_EVENT_ENC_BIT ? "ENC " : "",
		   irq_event & VPU_IRQ_EVENT_ENC_IRQ_BIT ? "ENC_IRQ" : "");

	vpu_power_on();
	seq_printf(s, "\nENC Registers:\n");
	n = enc_dev.iosize >> 2;
	for (i = 0; i < n; i++) {
		seq_printf(s, "\tswreg%d = %08X\n", i, readl(enc_dev.hwregs + i));
	}
	seq_printf(s, "\nDEC Registers:\n");
	n = dec_dev.iosize >> 2;
	for (i = 0; i < n; i++) {
		seq_printf(s, "\tswreg%d = %08X\n", i, readl(dec_dev.hwregs + i));
	}
	vpu_power_off();
	return 0;
}

static int proc_vpu_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_vpu_show, NULL);
}

static const struct file_operations proc_vpu_fops = {
	.open		= proc_vpu_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init vpu_proc_init(void)
{
	proc_create("vpu", 0, NULL, &proc_vpu_fops);
	return 0;

}
late_initcall(vpu_proc_init);
#endif /* CONFIG_PROC_FS */


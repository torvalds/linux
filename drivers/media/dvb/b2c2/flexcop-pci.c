/*
 * This file is part of linux driver the digital TV devices equipped with B2C2 FlexcopII(b)/III
 *
 * flexcop-pci.c - covers the PCI part including DMA transfers.
 *
 * see flexcop.c for copyright information.
 */

#define FC_LOG_PREFIX "flexcop-pci"
#include "flexcop-common.h"

static int enable_pid_filtering = 1;
module_param(enable_pid_filtering, int, 0444);
MODULE_PARM_DESC(enable_pid_filtering, "enable hardware pid filtering: supported values: 0 (fullts), 1");

static int irq_chk_intv;
module_param(irq_chk_intv, int, 0644);
MODULE_PARM_DESC(irq_chk_intv, "set the interval for IRQ watchdog (currently just debugging).");

#ifdef CONFIG_DVB_B2C2_FLEXCOP_DEBUG
#define dprintk(level,args...) \
	do { if ((debug & level)) printk(args); } while (0)
#define DEBSTATUS ""
#else
#define dprintk(level,args...)
#define DEBSTATUS " (debugging is not enabled)"
#endif

#define deb_info(args...)  dprintk(0x01,args)
#define deb_reg(args...)   dprintk(0x02,args)
#define deb_ts(args...)    dprintk(0x04,args)
#define deb_irq(args...)   dprintk(0x08,args)
#define deb_chk(args...)   dprintk(0x10,args)

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "set debug level (1=info,2=regs,4=TS,8=irqdma (|-able))." DEBSTATUS);

#define DRIVER_VERSION "0.1"
#define DRIVER_NAME "Technisat/B2C2 FlexCop II/IIb/III Digital TV PCI Driver"
#define DRIVER_AUTHOR "Patrick Boettcher <patrick.boettcher@desy.de>"

struct flexcop_pci {
	struct pci_dev *pdev;

#define FC_PCI_INIT     0x01
#define FC_PCI_DMA_INIT 0x02
	int init_state;

	void __iomem *io_mem;
	u32 irq;
/* buffersize (at least for DMA1, need to be % 188 == 0,
 * this logic is required */
#define FC_DEFAULT_DMA1_BUFSIZE (1280 * 188)
#define FC_DEFAULT_DMA2_BUFSIZE (10 * 188)
	struct flexcop_dma dma[2];

	int active_dma1_addr; /* 0 = addr0 of dma1; 1 = addr1 of dma1 */
	u32 last_dma1_cur_pos; /* position of the pointer last time the timer/packet irq occured */
	int count;

	spinlock_t irq_lock;

	unsigned long last_irq;

	struct work_struct irq_check_work;

	struct flexcop_device *fc_dev;
};

static int lastwreg,lastwval,lastrreg,lastrval;

static flexcop_ibi_value flexcop_pci_read_ibi_reg (struct flexcop_device *fc, flexcop_ibi_register r)
{
	struct flexcop_pci *fc_pci = fc->bus_specific;
	flexcop_ibi_value v;
	v.raw = readl(fc_pci->io_mem + r);

	if (lastrreg != r || lastrval != v.raw) {
		lastrreg = r; lastrval = v.raw;
		deb_reg("new rd: %3x: %08x\n",r,v.raw);
	}

	return v;
}

static int flexcop_pci_write_ibi_reg(struct flexcop_device *fc, flexcop_ibi_register r, flexcop_ibi_value v)
{
	struct flexcop_pci *fc_pci = fc->bus_specific;

	if (lastwreg != r || lastwval != v.raw) {
		lastwreg = r; lastwval = v.raw;
		deb_reg("new wr: %3x: %08x\n",r,v.raw);
	}

	writel(v.raw, fc_pci->io_mem + r);
	return 0;
}

static void flexcop_pci_irq_check_work(void *data)
{
	struct flexcop_pci *fc_pci = data;
	struct flexcop_device *fc = fc_pci->fc_dev;

	flexcop_ibi_value v = fc->read_ibi_reg(fc,sram_dest_reg_714);

	flexcop_dump_reg(fc_pci->fc_dev,dma1_000,4);

	if (v.sram_dest_reg_714.net_ovflow_error)
		deb_chk("sram net_ovflow_error\n");
	if (v.sram_dest_reg_714.media_ovflow_error)
		deb_chk("sram media_ovflow_error\n");
	if (v.sram_dest_reg_714.cai_ovflow_error)
		deb_chk("sram cai_ovflow_error\n");
	if (v.sram_dest_reg_714.cai_ovflow_error)
		deb_chk("sram cai_ovflow_error\n");

	schedule_delayed_work(&fc_pci->irq_check_work,
			msecs_to_jiffies(irq_chk_intv < 100 ? 100 : irq_chk_intv));
}

/* When PID filtering is turned on, we use the timer IRQ, because small amounts
 * of data need to be passed to the user space instantly as well. When PID
 * filtering is turned off, we use the page-change-IRQ */
static irqreturn_t flexcop_pci_isr(int irq, void *dev_id)
{
	struct flexcop_pci *fc_pci = dev_id;
	struct flexcop_device *fc = fc_pci->fc_dev;
	flexcop_ibi_value v;
	irqreturn_t ret = IRQ_HANDLED;

	spin_lock_irq(&fc_pci->irq_lock);

	v = fc->read_ibi_reg(fc,irq_20c);

   /* errors */
	if (v.irq_20c.Data_receiver_error)
		deb_chk("data receiver error\n");
	if (v.irq_20c.Continuity_error_flag)
		deb_chk("Contunuity error flag is set\n");
	if (v.irq_20c.LLC_SNAP_FLAG_set)
		deb_chk("LLC_SNAP_FLAG_set is set\n");
	if (v.irq_20c.Transport_Error)
		deb_chk("Transport error\n");

	if ((fc_pci->count % 1000) == 0)
		deb_chk("%d valid irq took place so far\n",fc_pci->count);

	if (v.irq_20c.DMA1_IRQ_Status == 1) {
		if (fc_pci->active_dma1_addr == 0)
			flexcop_pass_dmx_packets(fc_pci->fc_dev,fc_pci->dma[0].cpu_addr0,fc_pci->dma[0].size / 188);
		else
			flexcop_pass_dmx_packets(fc_pci->fc_dev,fc_pci->dma[0].cpu_addr1,fc_pci->dma[0].size / 188);

		deb_irq("page change to page: %d\n",!fc_pci->active_dma1_addr);
		fc_pci->active_dma1_addr = !fc_pci->active_dma1_addr;
	} else if (v.irq_20c.DMA1_Timer_Status == 1) {
		/* for the timer IRQ we only can use buffer dmx feeding, because we don't have
		 * complete TS packets when reading from the DMA memory */
		dma_addr_t cur_addr =
			fc->read_ibi_reg(fc,dma1_008).dma_0x8.dma_cur_addr << 2;
		u32 cur_pos = cur_addr - fc_pci->dma[0].dma_addr0;

		deb_irq("%u irq: %08x cur_addr: %llx: cur_pos: %08x, last_cur_pos: %08x ",
				jiffies_to_usecs(jiffies - fc_pci->last_irq),
				v.raw, (unsigned long long)cur_addr, cur_pos,
				fc_pci->last_dma1_cur_pos);
		fc_pci->last_irq = jiffies;

		/* buffer end was reached, restarted from the beginning
		 * pass the data from last_cur_pos to the buffer end to the demux
		 */
		if (cur_pos < fc_pci->last_dma1_cur_pos) {
			deb_irq(" end was reached: passing %d bytes ",(fc_pci->dma[0].size*2 - 1) - fc_pci->last_dma1_cur_pos);
			flexcop_pass_dmx_data(fc_pci->fc_dev,
					fc_pci->dma[0].cpu_addr0 + fc_pci->last_dma1_cur_pos,
					(fc_pci->dma[0].size*2) - fc_pci->last_dma1_cur_pos);
			fc_pci->last_dma1_cur_pos = 0;
		}

		if (cur_pos > fc_pci->last_dma1_cur_pos) {
			deb_irq(" passing %d bytes ",cur_pos - fc_pci->last_dma1_cur_pos);
			flexcop_pass_dmx_data(fc_pci->fc_dev,
					fc_pci->dma[0].cpu_addr0 + fc_pci->last_dma1_cur_pos,
					cur_pos - fc_pci->last_dma1_cur_pos);
		}
		deb_irq("\n");

		fc_pci->last_dma1_cur_pos = cur_pos;
		fc_pci->count++;
	} else {
		deb_irq("isr for flexcop called, apparently without reason (%08x)\n",v.raw);
		ret = IRQ_NONE;
	}

	spin_unlock_irq(&fc_pci->irq_lock);

	return ret;
}

static int flexcop_pci_stream_control(struct flexcop_device *fc, int onoff)
{
	struct flexcop_pci *fc_pci = fc->bus_specific;
	if (onoff) {
		flexcop_dma_config(fc,&fc_pci->dma[0],FC_DMA_1);
		flexcop_dma_config(fc,&fc_pci->dma[1],FC_DMA_2);

		flexcop_dma_config_timer(fc,FC_DMA_1,0);

		flexcop_dma_xfer_control(fc,FC_DMA_1,FC_DMA_SUBADDR_0 | FC_DMA_SUBADDR_1,1);
		deb_irq("DMA xfer enabled\n");

		fc_pci->last_dma1_cur_pos = 0;
		flexcop_dma_control_timer_irq(fc,FC_DMA_1,1);
		deb_irq("IRQ enabled\n");

//		fc_pci->active_dma1_addr = 0;
//		flexcop_dma_control_size_irq(fc,FC_DMA_1,1);

		if (irq_chk_intv > 0)
			schedule_delayed_work(&fc_pci->irq_check_work,
					msecs_to_jiffies(irq_chk_intv < 100 ? 100 : irq_chk_intv));
	} else {
		if (irq_chk_intv > 0)
			cancel_delayed_work(&fc_pci->irq_check_work);

		flexcop_dma_control_timer_irq(fc,FC_DMA_1,0);
		deb_irq("IRQ disabled\n");

//		flexcop_dma_control_size_irq(fc,FC_DMA_1,0);

		flexcop_dma_xfer_control(fc,FC_DMA_1,FC_DMA_SUBADDR_0 | FC_DMA_SUBADDR_1,0);
		deb_irq("DMA xfer disabled\n");
	}

	return 0;
}

static int flexcop_pci_dma_init(struct flexcop_pci *fc_pci)
{
	int ret;
	if ((ret = flexcop_dma_allocate(fc_pci->pdev,&fc_pci->dma[0],FC_DEFAULT_DMA1_BUFSIZE)) != 0)
		return ret;

	if ((ret = flexcop_dma_allocate(fc_pci->pdev,&fc_pci->dma[1],FC_DEFAULT_DMA2_BUFSIZE)) != 0) {
		flexcop_dma_free(&fc_pci->dma[0]);
		return ret;
	}

	flexcop_sram_set_dest(fc_pci->fc_dev,FC_SRAM_DEST_MEDIA | FC_SRAM_DEST_NET, FC_SRAM_DEST_TARGET_DMA1);
	flexcop_sram_set_dest(fc_pci->fc_dev,FC_SRAM_DEST_CAO   | FC_SRAM_DEST_CAI, FC_SRAM_DEST_TARGET_DMA2);

	fc_pci->init_state |= FC_PCI_DMA_INIT;

	return ret;
}

static void flexcop_pci_dma_exit(struct flexcop_pci *fc_pci)
{
	if (fc_pci->init_state & FC_PCI_DMA_INIT) {
		flexcop_dma_free(&fc_pci->dma[0]);
		flexcop_dma_free(&fc_pci->dma[1]);
	}
	fc_pci->init_state &= ~FC_PCI_DMA_INIT;
}

static int flexcop_pci_init(struct flexcop_pci *fc_pci)
{
	int ret;
	u8 card_rev;

	pci_read_config_byte(fc_pci->pdev, PCI_CLASS_REVISION, &card_rev);
	info("card revision %x", card_rev);

	if ((ret = pci_enable_device(fc_pci->pdev)) != 0)
		return ret;

	pci_set_master(fc_pci->pdev);

	/* enable interrupts */
	// pci_write_config_dword(pdev, 0x6c, 0x8000);

	if ((ret = pci_request_regions(fc_pci->pdev, DRIVER_NAME)) != 0)
		goto err_pci_disable_device;

	fc_pci->io_mem = pci_iomap(fc_pci->pdev, 0, 0x800);

	if (!fc_pci->io_mem) {
		err("cannot map io memory\n");
		ret = -EIO;
		goto err_pci_release_regions;
	}

	pci_set_drvdata(fc_pci->pdev, fc_pci);

	if ((ret = request_irq(fc_pci->pdev->irq, flexcop_pci_isr,
					IRQF_SHARED, DRIVER_NAME, fc_pci)) != 0)
		goto err_pci_iounmap;

	spin_lock_init(&fc_pci->irq_lock);

	fc_pci->init_state |= FC_PCI_INIT;
	return ret;

err_pci_iounmap:
	pci_iounmap(fc_pci->pdev, fc_pci->io_mem);
	pci_set_drvdata(fc_pci->pdev, NULL);
err_pci_release_regions:
	pci_release_regions(fc_pci->pdev);
err_pci_disable_device:
	pci_disable_device(fc_pci->pdev);
	return ret;
}

static void flexcop_pci_exit(struct flexcop_pci *fc_pci)
{
	if (fc_pci->init_state & FC_PCI_INIT) {
		free_irq(fc_pci->pdev->irq, fc_pci);
		pci_iounmap(fc_pci->pdev, fc_pci->io_mem);
		pci_set_drvdata(fc_pci->pdev, NULL);
		pci_release_regions(fc_pci->pdev);
		pci_disable_device(fc_pci->pdev);
	}
	fc_pci->init_state &= ~FC_PCI_INIT;
}


static int flexcop_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct flexcop_device *fc;
	struct flexcop_pci *fc_pci;
	int ret = -ENOMEM;

	if ((fc = flexcop_device_kmalloc(sizeof(struct flexcop_pci))) == NULL) {
		err("out of memory\n");
		return -ENOMEM;
	}

/* general flexcop init */
	fc_pci = fc->bus_specific;
	fc_pci->fc_dev = fc;

	fc->read_ibi_reg = flexcop_pci_read_ibi_reg;
	fc->write_ibi_reg = flexcop_pci_write_ibi_reg;
	fc->i2c_request = flexcop_i2c_request;
	fc->get_mac_addr = flexcop_eeprom_check_mac_addr;

	fc->stream_control = flexcop_pci_stream_control;

	if (enable_pid_filtering)
		info("will use the HW PID filter.");
	else
		info("will pass the complete TS to the demuxer.");

	fc->pid_filtering = enable_pid_filtering;
	fc->bus_type = FC_PCI;

	fc->dev = &pdev->dev;
	fc->owner = THIS_MODULE;

/* bus specific part */
	fc_pci->pdev = pdev;
	if ((ret = flexcop_pci_init(fc_pci)) != 0)
		goto err_kfree;

/* init flexcop */
	if ((ret = flexcop_device_initialize(fc)) != 0)
		goto err_pci_exit;

/* init dma */
	if ((ret = flexcop_pci_dma_init(fc_pci)) != 0)
		goto err_fc_exit;

	INIT_WORK(&fc_pci->irq_check_work, flexcop_pci_irq_check_work, fc_pci);

	return ret;

err_fc_exit:
	flexcop_device_exit(fc);
err_pci_exit:
	flexcop_pci_exit(fc_pci);
err_kfree:
	flexcop_device_kfree(fc);
	return ret;
}

/* in theory every _exit function should be called exactly two times,
 * here and in the bail-out-part of the _init-function
 */
static void flexcop_pci_remove(struct pci_dev *pdev)
{
	struct flexcop_pci *fc_pci = pci_get_drvdata(pdev);

	flexcop_pci_dma_exit(fc_pci);
	flexcop_device_exit(fc_pci->fc_dev);
	flexcop_pci_exit(fc_pci);
	flexcop_device_kfree(fc_pci->fc_dev);
}

static struct pci_device_id flexcop_pci_tbl[] = {
	{ PCI_DEVICE(0x13d0, 0x2103) },
/*	{ PCI_DEVICE(0x13d0, 0x2200) }, ? */
	{ },
};

MODULE_DEVICE_TABLE(pci, flexcop_pci_tbl);

static struct pci_driver flexcop_pci_driver = {
	.name     = "b2c2_flexcop_pci",
	.id_table = flexcop_pci_tbl,
	.probe    = flexcop_pci_probe,
	.remove   = flexcop_pci_remove,
};

static int __init flexcop_pci_module_init(void)
{
	return pci_register_driver(&flexcop_pci_driver);
}

static void __exit flexcop_pci_module_exit(void)
{
	pci_unregister_driver(&flexcop_pci_driver);
}

module_init(flexcop_pci_module_init);
module_exit(flexcop_pci_module_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_NAME);
MODULE_LICENSE("GPL");

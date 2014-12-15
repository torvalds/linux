#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/spi/spi.h>
#include <mach/pinmux.h>
#include <mach/am_regs.h>
#include <mach/spicc.h>
#include <linux/of_address.h>
#ifdef CONFIG_OF
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/of.h>
#else
#include <mach/gpio.h>
#include <mach/gpio_data.h>
#endif

/**
 * struct spicc
 * @lock: spinlock for SPICC controller.
  * @msg_queue: link with the spi message list.
 * @wq: work queque
 * @work: work
 * @master: spi master alloc for this driver.
 * @spi: spi device on working.
 * @regs: the start register address of this SPICC controller.
 */
struct spicc {
	spinlock_t lock;
	struct list_head msg_queue;
	struct workqueue_struct *wq;
	struct work_struct work;			
	struct spi_master	*master;
	struct spi_device	*spi;
	struct class cls;


	struct spicc_regs __iomem *regs;
#ifdef CONFIG_OF
	struct pinctrl *pinctrl;
#else
	pinmux_set_t pinctrl;
#endif
};

static bool spicc_dbgf = 1;
#define spicc_dbg(fmt, args...)  { if(spicc_dbgf) \
					printk("[spicc]: " fmt, ## args); }


static void spicc_dump(struct spicc *spicc)
{
	spicc_dbg("rxdata(0x%8x)    = 0x%x\n", &spicc->regs->rxdata, spicc->regs->rxdata);
	spicc_dbg("txdata(0x%8x)    = 0x%x\n", &spicc->regs->txdata, spicc->regs->txdata);
	spicc_dbg("conreg(0x%8x)    = 0x%x\n", &spicc->regs->conreg, *((volatile unsigned int *)(&spicc->regs->conreg)));
	spicc_dbg("intreg(0x%8x)    = 0x%x\n", &spicc->regs->intreg, *((volatile unsigned int *)(&spicc->regs->intreg)));
	spicc_dbg("dmareg(0x%8x)    = 0x%x\n", &spicc->regs->dmareg, *((volatile unsigned int *)(&spicc->regs->dmareg)));
	spicc_dbg("statreg(0x%8x)   = 0x%x\n", &spicc->regs->statreg, *((volatile unsigned int *)(&spicc->regs->statreg)));
	spicc_dbg("periodreg(0x%8x) = 0x%x\n", &spicc->regs->periodreg, spicc->regs->periodreg);
	spicc_dbg("testreg(0x%8x)   = 0x%x\n", &spicc->regs->testreg, spicc->regs->testreg);
}

static void spicc_chip_select(struct spicc *spicc, bool select)
{
  u8 chip_select = spicc->spi->chip_select;
  int cs_gpio = spicc->spi->cs_gpio;
  bool ss_pol = (spicc->spi->mode & SPI_CS_HIGH) ? 1 : 0;

  if (spicc->spi->mode & SPI_NO_CS) return;
  if (cs_gpio > 0) {
    amlogic_gpio_direction_output(cs_gpio, ss_pol ? select : !select, "spicc_cs");
  }
  else if (chip_select < spicc->master->num_chipselect) {
  	cs_gpio = spicc->master->cs_gpios[chip_select];
	  if ((cs_gpio = spicc->master->cs_gpios[chip_select]) > 0) {
	    amlogic_gpio_direction_output(cs_gpio, ss_pol ? select : !select, "spicc_cs");
	  }
	  else {
	    spicc->regs->conreg.chip_select = chip_select;
	    spicc->regs->conreg.ss_pol = ss_pol;
	    spicc->regs->conreg.ss_ctl = ss_pol;
	  }
  }
}


static void spicc_set_mode(struct spicc *spicc, u8 mode) 
{    
  spicc->regs->conreg.clk_pha = (mode & SPI_CPHA) ? 1:0;
  spicc->regs->conreg.clk_pol = (mode & SPI_CPOL) ? 1:0;
  spicc->regs->conreg.drctl = 0; //data ready, 0-ignore, 1-falling edge, 2-rising edge
  spicc_dbg("mode = 0x%x\n", mode);
}


static void spicc_set_clk(struct spicc *spicc, int speed) 
{	
	struct clk *sys_clk = clk_get_sys("clk81", NULL);
	unsigned sys_clk_rate = clk_get_rate(sys_clk);
	unsigned div, mid_speed;
  
  // actually, speed = sys_clk_rate / 2^(conreg.data_rate_div+2)
  mid_speed = (sys_clk_rate * 3) >> 4;
  for(div=0; div<7; div++) {
    if (speed >= mid_speed) break;    
    mid_speed >>= 1;
  }
  spicc->regs->conreg.data_rate_div = div;
  spicc_dbg("sys_clk_rate=%d, speed=%d, div=%d\n", sys_clk_rate, speed, div);
}


static int spicc_hw_xfer(struct spicc *spicc, u8 *txp, u8 *rxp, int len)
{
	u8 dat;
	int i, num,retry;
	
	spicc_dbg("length = %d\n", len);
	while (len > 0) {
		num = (len > SPICC_FIFO_SIZE) ? SPICC_FIFO_SIZE : len;
		for (i=0; i<num; i++) {
			dat = txp ? (*txp++) : 0;
			spicc->regs->txdata = dat;
			//spicc_dbg("txdata[%d] = 0x%x\n", i, dat);
		}
		for (i=0; i<num; i++) {
			retry = 100;
			while (!spicc->regs->statreg.rx_ready && retry--) {udelay(1);}
			dat = spicc->regs->rxdata;
			if (rxp) *rxp++ = dat;
			//spicc_dbg("rxdata[%d] = 0x%x\n", i, dat);
			if (!retry) {
			  printk("error: spicc timeout\n");
			  return -ETIMEDOUT;
			}
		}
		len -= num;
	}
	return 0;
}

/* mode: SPICC_DMA_MODE/SPICC_PIO_MODE
 */
static void spicc_hw_init(struct spicc *spicc)
{
	spicc_clk_gate_on();
	udelay(10);
  spicc->regs->testreg |= 1<<24; //clock free enable
  spicc->regs->conreg.enable = 0; // disable SPICC
  spicc->regs->conreg.mode = 1; // 0-slave, 1-master
  spicc->regs->conreg.xch = 0;
  spicc->regs->conreg.smc = SPICC_PIO;
  spicc->regs->conreg.bits_per_word = 7; // default bits width 8
  spicc_set_mode(spicc, SPI_MODE_0); // default mode 0
  spicc_set_clk(spicc, 3000000); // default speed 3M
  spicc->regs->conreg.ss_ctl = 1;
  spicc_dump(spicc);
}


//setting clock and pinmux here
static int spicc_setup(struct spi_device	*spi)
{
    return 0;
}

static void spicc_cleanup(struct spi_device *spi)
{
	if (spi->modalias)
		kfree(spi->modalias);
}

static void spicc_handle_one_msg(struct spicc *spicc, struct spi_message *m)
{
	struct spi_device *spi = m->spi;
	struct spi_transfer *t;
  int ret = 0;

  // re-set to prevent others from disable the SPICC clk gate
  spicc_clk_gate_on();
  if (spicc->spi != spi) {
    spicc->spi = spi;
    spicc_set_clk(spicc, spi->max_speed_hz);	    
	  spicc_set_mode(spicc, spi->mode);
	}
  spicc_chip_select(spicc, 1); // select
  spicc->regs->conreg.enable = 1; // enable spicc
  
	list_for_each_entry(t, &m->transfers, transfer_list) {
  	if((spi->max_speed_hz != t->speed_hz) && t->speed_hz) {
  	  spicc_set_clk(spicc, t->speed_hz);	    
  	}  
		if (spicc_hw_xfer(spicc,(u8 *)t->tx_buf, (u8 *)t->rx_buf, t->len) < 0) {
			goto spicc_handle_end;
		}
		m->actual_length += t->len;
		if (t->delay_usecs) {
			udelay(t->delay_usecs);
		}
	}

spicc_handle_end:
  spicc->regs->conreg.enable = 0; // disable spicc
  spicc_chip_select(spicc, 0); // unselect
  spicc_clk_gate_off();

  m->status = ret;
  if(m->context) {
    m->complete(m->context);
  }
}

static int spicc_transfer(struct spi_device *spi, struct spi_message *m)
{
	struct spicc *spicc = spi_master_get_devdata(spi->master);
	unsigned long flags;

	m->actual_length = 0;
	m->status = -EINPROGRESS;

	spin_lock_irqsave(&spicc->lock, flags);
	list_add_tail(&m->queue, &spicc->msg_queue);
	queue_work(spicc->wq, &spicc->work);
	spin_unlock_irqrestore(&spicc->lock, flags);

	return 0;
}

static void spicc_work(struct work_struct *work)
{
	struct spicc *spicc = container_of(work, struct spicc, work);
	struct spi_message *m;
	unsigned long flags;

	spin_lock_irqsave(&spicc->lock, flags);
	while (!list_empty(&spicc->msg_queue)) {
		m = container_of(spicc->msg_queue.next, struct spi_message, queue);
		list_del_init(&m->queue);
		spin_unlock_irqrestore(&spicc->lock, flags);
		spicc_handle_one_msg(spicc, m);
		spin_lock_irqsave(&spicc->lock, flags);
	}
	spin_unlock_irqrestore(&spicc->lock, flags);
}


static ssize_t store_test(struct class *class, struct class_attribute *attr,	const char *buf, size_t count)
{
	struct spicc *spicc = container_of(class, struct spicc, cls);
	unsigned int i, cs_gpio, speed, mode, num;
	u8 wbuf[4]={0}, rbuf[128]={0};
	unsigned long flags;
//	unsigned char cs_gpio_name[20];

	if (buf[0] == 'h') {
		printk("SPI device test help\n");
		printk("You can test the SPI device even without its driver through this sysfs node\n");
		printk("echo cs_gpio speed num [wdata1 wdata2 wdata3 wdata4] >test\n");
		return count;
	}
    
	i = sscanf(buf, "%d%d%d%d%x%x%x%x", &cs_gpio, &speed, &mode, &num, 
		(unsigned int *)&wbuf[0], (unsigned int *)&wbuf[1], (unsigned int *)&wbuf[2], (unsigned int *)&wbuf[3]);
	printk("cs_gpio=%d, speed=%d, mode=%d, num=%d\n", cs_gpio, speed, mode, num);
	if ((i<(num+4)) || (!cs_gpio) || (!speed) || (num > sizeof(wbuf))) {
		printk("invalid data\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&spicc->lock, flags);	
	amlogic_gpio_request(cs_gpio, "spicc_cs");
	amlogic_gpio_direction_output(cs_gpio, 0, "spicc_cs");
  spicc_clk_gate_on();
 	spicc_set_clk(spicc, speed);	    
	spicc_set_mode(spicc, mode);
  spicc->regs->conreg.enable = 1; // enable spicc
//	spicc_dump(spicc);
	
	spicc_hw_xfer(spicc, wbuf, rbuf, num);
	printk("read back data: ");
	for (i=0; i<num; i++) {
		printk("0x%x, ", rbuf[i]);
	}
	printk("\n");
	
	spicc->regs->conreg.enable = 0; // disable spicc
	spicc_clk_gate_off();
	amlogic_gpio_direction_input(cs_gpio, "spicc_cs");
	amlogic_gpio_free(cs_gpio, "spicc_cs");
	spin_unlock_irqrestore(&spicc->lock, flags);
    
	return count;
}
static struct class_attribute spicc_class_attrs[] = {
    __ATTR(test,  S_IWUSR, NULL,    store_test),
    __ATTR_NULL
};

static int spicc_probe(struct platform_device *pdev)
{
	struct spicc_platform_data *pdata;
	struct spi_master	*master;
	struct spicc *spicc;
	int i, gpio, ret;
	
#ifdef CONFIG_OF
	struct spicc_platform_data spicc_pdata;
	const char *prop_name;
	
	BUG_ON(!pdev->dev.of_node);
	pdata = &spicc_pdata;

	ret = of_property_read_u32(pdev->dev.of_node,"device_id",&pdata->device_id);
	if(ret) {
		dev_err(&pdev->dev, "match device_id failed!\n");
		return -ENODEV;
	}
	dev_info(&pdev->dev, "device_id = %d \n", pdata->device_id);

	ret = of_property_read_string(pdev->dev.of_node, "pinctrl-names", &prop_name);
	if(ret || IS_ERR(prop_name)) {
		dev_err(&pdev->dev, "match pinctrl-names failed!\n");
		return -ENODEV;
	}
	pdata->pinctrl = devm_pinctrl_get_select(&pdev->dev, prop_name);
	if(IS_ERR(pdata->pinctrl)) {
		dev_err(&pdev->dev, "pinmux error\n");
		return -ENODEV;
	}
	dev_info(&pdev->dev, "pinctrl_name = %s\n", prop_name);
	
	ret = of_property_read_u32(pdev->dev.of_node,"num_chipselect",&pdata->num_chipselect);
	if(ret) {
		dev_err(&pdev->dev, "match num_chipselect failed!\n");
		return -ENODEV;
	}
 	dev_info(&pdev->dev, "num_chipselect = %d\n", pdata->num_chipselect);

	pdata->cs_gpios = kzalloc(sizeof(int)*pdata->num_chipselect, GFP_KERNEL);
	for (i=0; i<pdata->num_chipselect; i++) {
		ret = of_property_read_string_index(pdev->dev.of_node, "cs_gpios", i, &prop_name);
		if(ret || IS_ERR(prop_name) || ((gpio = amlogic_gpio_name_map_num(prop_name)) < 0)) {
			dev_err(&pdev->dev, "match cs_gpios[%d](%s) failed!\n", i, prop_name);
      kzfree(pdata->cs_gpios);
			return -ENODEV;
		}
		else {
   		*(pdata->cs_gpios+i) = gpio;
 			dev_info(&pdev->dev, "cs_gpios[%d] = %s(%d)\n", i, prop_name, gpio);
		}
	} 	
 	
  pdata->regs = (struct spicc_regs __iomem *)of_iomap(pdev->dev.of_node, 0);
	dev_info(&pdev->dev, "regs = %x\n", pdata->regs);
#else
	pdata = (struct spicc_platform_data *)pdev->dev.platform_data
	BUG_ON(!pdata);	
#endif
	for (i=0; i<pdata->num_chipselect; i++) {
		gpio = pdata->cs_gpios[i];
		if (amlogic_gpio_request(gpio, "spicc_cs")) {
			dev_err(&pdev->dev, "request chipselect gpio(%d) failed!\n", i);
      kzfree(pdata->cs_gpios);
			return -ENODEV;
		}
		amlogic_gpio_direction_output(gpio, 1, "spicc_cs");
	}
	
	master = spi_alloc_master(&pdev->dev, sizeof *spicc);
	if (master == NULL) {
		dev_err(&pdev->dev, "allocate spi master failed!\n");
		return -ENOMEM;
	}
  master->bus_num = pdata->device_id;
  master->num_chipselect = pdata->num_chipselect;
  master->cs_gpios = pdata->cs_gpios;
	master->setup = spicc_setup;
	master->transfer = spicc_transfer;
	master->cleanup = spicc_cleanup;
	ret = spi_register_master(master);
	if (ret < 0) {
			dev_err(&pdev->dev, "register spi master failed! (%d)\n", ret);
			goto err;
	}
	
	spicc = spi_master_get_devdata(master);
	spicc->master = master;	
	spicc->regs = pdata->regs;
	spicc->pinctrl = pdata->pinctrl;
	dev_set_drvdata(&pdev->dev, spicc);
	INIT_WORK(&spicc->work, spicc_work);
	spicc->wq = create_singlethread_workqueue(dev_name(master->dev.parent));
	if (spicc->wq == NULL) {
		ret = -EBUSY;
		goto err;
	}		  		  
	spin_lock_init(&spicc->lock);
	INIT_LIST_HEAD(&spicc->msg_queue);
		
	spicc_hw_init(spicc);

  /*setup class*/
  spicc->cls.name = kzalloc(10, GFP_KERNEL);
  sprintf((char*)spicc->cls.name, "spicc%d", master->bus_num);
  spicc->cls.class_attrs = spicc_class_attrs;
  if ((ret = class_register(&spicc->cls)) < 0) {
		dev_err(&pdev->dev, "register class failed! (%d)\n", ret);
	}
	
	dev_info(&pdev->dev, "SPICC init ok \n");
	return ret;
err:
	spi_master_put(master);
	return ret;
}

static int spicc_remove(struct platform_device *pdev)
{
	struct spicc *spicc;

	spicc = (struct spicc *)dev_get_drvdata(&pdev->dev);
	spi_unregister_master(spicc->master);
	destroy_workqueue(spicc->wq);
#ifdef CONFIG_OF
	if(spicc->pinctrl) {
		devm_pinctrl_put(spicc->pinctrl);
	}
#else
    pinmux_clr(&spicc->pinctrl);
#endif
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id spicc_of_match[]={
	{	.compatible = "amlogic, spicc", },
	{},
};
#else
#define spicc_of_match NULL
#endif

static struct platform_driver spicc_driver = { 
	.probe = spicc_probe, 
	.remove = spicc_remove, 
	.driver = {
			.name = "spicc", 
      .of_match_table = spicc_of_match,
			.owner = THIS_MODULE, 
		}, 
};

static int __init spicc_init(void) 
{	
	return platform_driver_register(&spicc_driver);
}

static void __exit spicc_exit(void) 
{	
	platform_driver_unregister(&spicc_driver);
} 

subsys_initcall(spicc_init);
module_exit(spicc_exit);

MODULE_DESCRIPTION("Amlogic SPICC driver");
MODULE_LICENSE("GPL");

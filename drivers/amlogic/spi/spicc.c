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

#include <mach/irqs.h>

// SPICC Ingerrupt Number
#define AMLOGIC_SPI_IRQ     INT_SPI_2

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

	struct spi_master	*master;
	struct spi_device	*spi;
	struct class cls;

	struct spicc_regs __iomem *regs;
#ifdef CONFIG_OF
	struct pinctrl *pinctrl;
#else
	pinmux_set_t pinctrl;
#endif

    int     cur_speed;
    u8      cur_mode;
    u8      cur_bits_per_word;

    // SPI IRQ USED
    u8      *cur_msg_txbuf;
    u8      *cur_msg_rxbuf;
    int     cur_msg_txp;
    int     cur_msg_rxp;
    int     cur_msg_len;
};

#if defined CONFIG_AMLOGIC_SPICC_MASTER_DEBUG
    const bool spicc_dbgf = 1;
#else
    const bool spicc_dbgf = 0;
#endif

#define spicc_dbg(fmt, args...)  { if(spicc_dbgf) \
					printk("[spicc]: " fmt, ## args); }

static void spicc_dump(struct spicc *spicc)
{
	spicc_dbg("rxdata(0x%p)    = 0x%x\n", &spicc->regs->rxdata, spicc->regs->rxdata);
	spicc_dbg("txdata(0x%p)    = 0x%x\n", &spicc->regs->txdata, spicc->regs->txdata);
	spicc_dbg("conreg(0x%p)    = 0x%x\n", &spicc->regs->conreg, *((volatile unsigned int *)(&spicc->regs->conreg)));
	spicc_dbg("intreg(0x%p)    = 0x%x\n", &spicc->regs->intreg, *((volatile unsigned int *)(&spicc->regs->intreg)));
	spicc_dbg("dmareg(0x%p)    = 0x%x\n", &spicc->regs->dmareg, *((volatile unsigned int *)(&spicc->regs->dmareg)));
	spicc_dbg("statreg(0x%p)   = 0x%x\n", &spicc->regs->statreg, *((volatile unsigned int *)(&spicc->regs->statreg)));
	spicc_dbg("periodreg(0x%p) = 0x%x\n", &spicc->regs->periodreg, spicc->regs->periodreg);
	spicc_dbg("testreg(0x%p)   = 0x%x\n", &spicc->regs->testreg, spicc->regs->testreg);
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
    spicc->cur_mode = mode;
    spicc_dbg("mode = 0x%x\n", mode);
}

//
// available spi clock out table
//
// div = 0 : 39,843,750 Hz, div = 1 : 19,921,875 Hz
// div = 2 :  9,960,937 Hz, div = 3 :  4,980,468 Hz
// div = 4 :  2,490,234 Hz, div = 5 :  1,245,117 Hz
// div = 6 :    625,558 Hz, div = 7 :    311,279 Hz
//
static void spicc_set_clk(struct spicc *spicc, int speed) 
{	
    struct clk *sys_clk = clk_get_sys("clk81", NULL);
    unsigned long sys_clk_rate = clk_get_rate(sys_clk);
    unsigned long div, mid_speed, div_val;
  
    // actually, speed = sys_clk_rate / 2^(conreg.data_rate_div+2)
    mid_speed = (sys_clk_rate * 3) >> 4;
    for(div=0; div<7; div++) {
        if (speed >= mid_speed) break;
        mid_speed >>= 1;
    }
    spicc->regs->conreg.data_rate_div = div;
    spicc->cur_speed = speed;
    div_val = (div + 2);
    spicc_dbg("sys_clk_rate = %ld, speed = %d, div = %ld, actually speed = %ld\n",
        sys_clk_rate, speed, div, (sys_clk_rate >> div_val));
}

/*
    mode: SPICC_DMA_MODE/SPICC_PIO_MODE
*/
static void spicc_hw_init(struct spicc *spicc)
{
    // SPICC clock enable
    spicc_clk_gate_on();
    udelay(10);

    // clock free enable
    spicc->regs->testreg |= 1<<24;

    // SPICC module enable bit.
    // 0 : disable, 1 : enable
    spicc->regs->conreg.enable = 0;

    // Mode of the SPI module
    // 0 : slave, 1 : master
    spicc->regs->conreg.mode = 1;

    // Setting XCH will issue a burst when SMC is 0, and this bit will be self cleared
    // after burst is finished.
    spicc->regs->conreg.xch = 0;

    // Start mode control
    // 0 : burst will start when XCH is set to 1
    // 1 : burst will start when TXFIFO is not empty(DMA mode)
    spicc->regs->conreg.smc = SPICC_PIO;

    // bit number of one word/package
    // default bits width 8
    spicc->cur_bits_per_word = 8;
    spicc->regs->conreg.bits_per_word = spicc->cur_bits_per_word -1; 

    // SPI Mode Setup
    // SPI_MODE_0 : SPI_CPOL = 0, SPI_CPHA = 0
    // SPI_MODE_1 : SPI_CPOL = 0, SPI_CPHA = 1
    // SPI_MODE_2 : SPI_CPOL = 1, SPI_CPHA = 0
    // SPI_MODE_3 : SPI_CPOL = 1, SPI_CPHA = 1
    spicc_set_mode(spicc, SPI_MODE_0); // default mode 0
    spicc->cur_mode = SPI_MODE_0;

    // SPI Clock Setup. Default clock speed 3Mhz
    spicc_set_clk(spicc, 3000000);
    spicc->cur_speed = 3000000;

    // Chip Selection output control in one burst of master mode
    // 0 : output 0 between each SPI transition
    // 1 : output 1 between each SPI transition
    spicc->regs->conreg.ss_ctl = 1;

    // Chip Selection polarity
    // 0 : Low active, 1 : High active
    spicc->regs->conreg.ss_pol = 0;

    // Debug Message (Register Dump)
    spicc_dump(spicc);
}

//setting clock and pinmux here
static int spicc_setup(struct spi_device *spi)
{
	struct spicc *spicc;

    if(!(spicc = spi_master_get_devdata(spi->master)))   return 0;

	if (spi->bits_per_word != 8 && spi->bits_per_word != 16 && spi->bits_per_word != 32) {
		dev_err(&spi->dev, "setup: %dbits/wrd not supported!\n", spi->bits_per_word);
		return  -EINVAL;
	}

    if((spicc->cur_bits_per_word != spi->bits_per_word) ||
       (spicc->cur_mode          != spi->mode)          ||
       (spicc->cur_speed         != spi->max_speed_hz)) {

        spicc_clk_gate_on();    udelay(10);

        spicc->regs->conreg.enable = 0; // disable spicc

        spicc_set_clk(spicc, spi->max_speed_hz);
        spicc_set_mode(spicc, spi->mode);

        spicc->cur_bits_per_word = spi->bits_per_word;
        spicc->regs->conreg.bits_per_word = spicc->cur_bits_per_word -1;

        spicc->regs->conreg.enable = 1; // enable spicc

        spicc_clk_gate_off();
        dev_info(&spi->dev, "%s : spi->bits_per_word = %d, spi->max_spped_hz = %d, spi->chip_select = %d, spi->mode = 0x%02X\n"
                , __func__
                , spi->bits_per_word
                , spi->max_speed_hz
                , spi->chip_select
                , spi->mode);
    }

    return 0;
}

static irqreturn_t spicc_irq_handler    (int irq, void*data)
{
	struct spicc *spicc = data;
	unsigned char   rdata;
	unsigned long   flags;

	spin_lock_irqsave(&spicc->lock, flags);

    if(spicc->regs->statreg.rx_ready)   {
	    rdata = spicc->regs->rxdata;
	    if(spicc->cur_msg_rxbuf)    spicc->cur_msg_rxbuf[spicc->cur_msg_rxp] = rdata;
        spicc->cur_msg_rxp++;
	}

    if(spicc->regs->statreg.tx_empty)   {
        if(spicc->cur_msg_len)   {
            spicc->regs->txdata =
                spicc->cur_msg_txbuf ? spicc->cur_msg_txbuf[spicc->cur_msg_txp] : 0x00;
            spicc->cur_msg_txp++;   spicc->cur_msg_len--;
        }
    }

	spin_unlock_irqrestore(&spicc->lock, flags);

    return  IRQ_HANDLED;
}

static int spicc_hw_xfer    (struct spicc *spicc, u8 *txp, u8 *rxp, int len)
{
	int retry;

	spicc_dbg("length = %d\n", len);

    if((len <= 0))  {
        pr_err("%s : error! len = %d\n", __func__, len);    return  -1;
    }

    // RX ready interrupt enable
    spicc->regs->intreg.rx_ready_en = 1;

	spicc->cur_msg_txbuf    = txp;	spicc->cur_msg_txp      = 1;
	spicc->cur_msg_rxbuf    = rxp;	spicc->cur_msg_rxp      = 0;
	spicc->cur_msg_len      = len -1;

    spicc->regs->txdata = spicc->cur_msg_txbuf ? spicc->cur_msg_txbuf[0] : 0x00;

    // wait for rxbuf data (Max 1ms)
    retry = 1000;
    while(retry--)    {
        usleep_range(1, 2);
        if(spicc->cur_msg_rxp == len)   break;
    }

    // RX ready interrupt disable
    spicc->regs->intreg.rx_ready_en = 0;

    if (!retry) {
        pr_err("error: spicc rxdata recv timeout\n");   return  -ETIMEDOUT;
    }
    return  0;
}

static int spicc_transfer_one_message(struct spi_master *master,
					    struct spi_message *m)
{
	struct spicc *spicc = spi_master_get_devdata(master);
    struct spi_device *spi = m->spi;
    struct spi_transfer *t;
    int ret = 0;

    // re-set to prevent others from disable the SPICC clk gate
    spicc_clk_gate_on();

    // enable spicc
    spicc->regs->conreg.enable = 1;

    if (spicc->spi != spi) {
        spicc->spi = spi;
        spicc_set_clk(spicc, spi->max_speed_hz);
        spicc_set_mode(spicc, spi->mode);
    }

    list_for_each_entry(t, &m->transfers, transfer_list) {

        spicc_chip_select(spicc, 1); // select

        if((spi->max_speed_hz != t->speed_hz) && t->speed_hz) {
            spicc_set_clk(spicc, t->speed_hz);
        }

        if ((ret = spicc_hw_xfer(spicc,(u8 *)t->tx_buf, (u8 *)t->rx_buf, t->len)) < 0) {
            goto spicc_handle_end;
        }

        m->actual_length += t->len;

        if (t->delay_usecs) {
            udelay(t->delay_usecs);
        }
    }

spicc_handle_end:
    spicc_chip_select(spicc, 0);    // unselect
    spicc->regs->conreg.enable = 0; // disable spicc
    spicc_clk_gate_off();

	m->status = ret;
    spi_finalize_current_message(spicc->master);

    return  0;
}

static 	ssize_t spicc_test (struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct spicc *spicc = dev_get_drvdata(dev);

	unsigned int i, cs_gpio, speed, mode, num;
	u8 wbuf[4]={0}, rbuf[128]={0};
	unsigned long flags;

	if (buf[0] == 'h') {
		printk("SPI device test help\n");
		printk("You can test the SPI device even without its driver through this sysfs node\n");
		printk("echo cs_gpio speed mode num [wdata1 wdata2 wdata3 wdata4] > test\n");
		return count;
	}

	i = sscanf(buf, "%d%d%d%d%x%x%x%x", &cs_gpio, &speed, &mode, &num,
		(unsigned int *)&wbuf[0], (unsigned int *)&wbuf[1], (unsigned int *)&wbuf[2], (unsigned int *)&wbuf[3]);

	printk("cs_gpio=%d, speed=%d, mode=%d, num=%d\n", cs_gpio, speed, mode, num);

	if ((i<(num+4)) || (!cs_gpio) || (!speed) || (num > sizeof(wbuf))) {
		printk("invalid data\n");   return -EINVAL;
	}

	spin_lock_irqsave(&spicc->lock, flags);
	amlogic_gpio_request(cs_gpio, "spicc_cs");
	amlogic_gpio_direction_output(cs_gpio, 0, "spicc_cs");

    spicc_clk_gate_on();    udelay(10);
    spicc_set_clk(spicc, speed);
    spicc_set_mode(spicc, mode);
    spicc->regs->conreg.enable = 1; // enable spicc

	spicc_dump(spicc);
	spin_unlock_irqrestore(&spicc->lock, flags);

	spicc_hw_xfer(spicc, wbuf, rbuf, num);

	spin_lock_irqsave(&spicc->lock, flags);

	printk("read back data: ");

	for (i=0; i<num; i++)   printk("0x%x, ", rbuf[i]);
	printk("\n");

	spicc->regs->conreg.enable = 0; // disable spicc
	spicc_clk_gate_off();
	amlogic_gpio_direction_input(cs_gpio, "spicc_cs");
	amlogic_gpio_free(cs_gpio, "spicc_cs");
	spin_unlock_irqrestore(&spicc->lock, flags);

	return count;
}

static	DEVICE_ATTR(test, S_IRWXUGO, NULL, spicc_test);

static struct attribute *spicc_sysfs_entries[] = {
	&dev_attr_test.attr,
	NULL
};

static struct attribute_group spicc_attr_group = {
	.name   = NULL,
	.attrs  = spicc_sysfs_entries,
};

static int spicc_probe(struct platform_device *pdev)
{
	struct spicc_platform_data *pdata;
	struct spi_master	*master;
	struct spicc *spicc;
	struct resource *res;
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

	pdata->cs_gpios = devm_kzalloc(&pdev->dev, sizeof(int)*pdata->num_chipselect, GFP_KERNEL);
	for (i=0; i<pdata->num_chipselect; i++) {
		ret = of_property_read_string_index(pdev->dev.of_node, "cs_gpios", i, &prop_name);
		if(ret || IS_ERR(prop_name) || ((gpio = amlogic_gpio_name_map_num(prop_name)) < 0)) {
			dev_err(&pdev->dev, "match cs_gpios[%d](%s) failed!\n", i, prop_name);
			return -ENODEV;
		}
		else {
            *(pdata->cs_gpios+i) = gpio;
 			dev_info(&pdev->dev, "cs_gpios[%d] = %s(%d)\n", i, prop_name, gpio);
		}
	} 	

    if(!(res = platform_get_resource(pdev, IORESOURCE_MEM, 0))) {
        dev_err(&pdev->dev, "Could not get memory resource!\n");
        return  -ENODEV;
    }
    if((pdata->regs = devm_request_and_ioremap(&pdev->dev, res)) == NULL)   {
        dev_err(&pdev->dev, "Could not request/map memory region!\n");
        return  -ENODEV;
    }
	dev_info(&pdev->dev, "regs = %p\n", pdata->regs);
#else
	pdata = (struct spicc_platform_data *)pdev->dev.platform_data
	BUG_ON(!pdata);	
#endif
	for (i=0; i<pdata->num_chipselect; i++) {
		gpio = pdata->cs_gpios[i];
		if (amlogic_gpio_request(gpio, "spicc_cs")) {
			dev_err(&pdev->dev, "request chipselect gpio(%d) failed!\n", i);
			return -ENODEV;
		}
		amlogic_gpio_direction_output(gpio, 1, "spicc_cs");
	}

	master = spi_alloc_master(&pdev->dev, sizeof *spicc);
	if (master == NULL) {
		dev_err(&pdev->dev, "allocate spi master failed!\n");
		return -ENOMEM;
	}

    master->dev.of_node = pdev->dev.of_node;

    master->bus_num = pdev->id = pdata->device_id;
    master->num_chipselect = pdata->num_chipselect;
    master->cs_gpios = pdata->cs_gpios;

	master->bits_per_word_mask = BIT(32 - 1) | BIT(16 - 1) | BIT(8 - 1);

	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_NO_CS;

	master->setup                   = spicc_setup;
    master->transfer_one_message    = spicc_transfer_one_message;

	spicc = spi_master_get_devdata(master);

	spicc->master   = master;
	spicc->regs     = pdata->regs;
	spicc->pinctrl  = pdata->pinctrl;

	dev_set_drvdata(&pdev->dev, spicc);

	spin_lock_init(&spicc->lock);
	spicc_hw_init(spicc);

    /*setup class*/
	if(sysfs_create_group(&pdev->dev.kobj, &spicc_attr_group) < 0)	{
		dev_err(&pdev->dev, "failed to create sysfs group !!\n");
		goto    err;
	}

    if((ret = request_irq(AMLOGIC_SPI_IRQ, spicc_irq_handler, IRQF_DISABLED, "spicc_irq", spicc)))    {
        dev_err(&pdev->dev, "Failed to register SPICC IRQ(%d)!\n", AMLOGIC_SPI_IRQ);
		goto    err;
    }
    else
        dev_info(&pdev->dev, "Register SPICC IRQ(%d) success!\n",  AMLOGIC_SPI_IRQ);

	if ((ret = spi_register_master(master)) < 0) {
        dev_err(&pdev->dev, "register spi master failed! (%d)\n", ret);
        goto err;
	}

    dev_info(&pdev->dev, "SPICC init ok \n");

    return ret;
err:
	spi_master_put(master);
	return ret;
}

static int spicc_remove(struct platform_device *pdev)
{
    struct spicc *spicc = dev_get_drvdata(&pdev->dev);
    struct spi_master *master = spicc->master;
    int i;

    spi_unregister_master(spicc->master);

	sysfs_remove_group(&pdev->dev.kobj, &spicc_attr_group);

    for(i = 0; i < master->num_chipselect; i++) {
        if(master->cs_gpios[i]) amlogic_gpio_free(master->cs_gpios[i], "spicc_cs");
    }

#ifdef CONFIG_OF
	if(spicc->pinctrl) {
		devm_pinctrl_put(spicc->pinctrl);
	}
#else
    pinmux_clr(&spicc->pinctrl);
#endif

    free_irq(AMLOGIC_SPI_IRQ, spicc);
    spi_master_put(master);

    dev_info(&pdev->dev, "SPICC remove OK \n");

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

module_init(spicc_init);
module_exit(spicc_exit);

MODULE_DESCRIPTION("Amlogic SPICC driver");
MODULE_LICENSE("GPL");

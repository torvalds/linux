static rmii_extclk_sel = 0;
static int rk30_vmac_register_set(void)
{
    //config rk30 vmac as rmii
    writel_relaxed(0x3 << 16 | 0x2, RK30_GRF_BASE + GRF_SOC_CON1);
    int val = readl_relaxed(RK30_GRF_BASE + GRF_IO_CON3);
    writel_relaxed(val | 0xf << 16 | 0xf, RK30_GRF_BASE + GRF_IO_CON3);
    val = readl(RK30_GRF_BASE + GRF_SOC_CON2);
    writel(0x1 << 6 | 0x1 << 22 | val, RK30_GRF_BASE + GRF_SOC_CON2);

    return 0;
}

static int rk30_rmii_io_init(void)
{
    int err;
    printk("enter %s \n",__func__);

    iomux_set(RMII_TXEN);
    iomux_set(RMII_TXD1);
    iomux_set(RMII_TXD0);
    iomux_set(RMII_RXD0);
    iomux_set(RMII_RXD1);
#ifdef RMII_EXT_CLK
    iomux_set(RMII_CLKIN);
#else
    iomux_set(RMII_CLKOUT);  
#endif
    iomux_set(RMII_RXERR);
    iomux_set(RMII_CRS);
    iomux_set(RMII_MD);
    iomux_set(RMII_MDCLK);

    if(INVALID_GPIO != PHY_PWR_EN_GPIO)
    {
     	//phy power gpio request
        iomux_set(PHY_PWR_EN_IOMUX); 
	err = gpio_request(PHY_PWR_EN_GPIO, "phy_power_en");
	if (err) {
	    printk("request phy power en pin faile ! \n");
	    return -1;
	}
        //phy power down
        gpio_direction_output(PHY_PWR_EN_GPIO, !PHY_PWR_EN_VALUE);
    }

    return 0;
}

static int rk30_rmii_io_deinit(void)
{
    //phy power down
    printk("enter %s \n",__func__);

    if(INVALID_GPIO != PHY_PWR_EN_GPIO)
    {
        gpio_direction_output(PHY_PWR_EN_GPIO, !PHY_PWR_EN_VALUE);
        //free
        gpio_free(PHY_PWR_EN_GPIO);
    }

    return 0;
}

static int rk30_rmii_power_control(int enable)
{
    printk("enter %s ,enable = %d \n",__func__,enable);

    if (enable){
	    iomux_set(RMII_TXEN);
	    iomux_set(RMII_TXD1);
	    iomux_set(RMII_TXD0);
	    iomux_set(RMII_RXD0);
	    iomux_set(RMII_RXD1);
#ifdef RMII_EXT_CLK
            iomux_set(RMII_CLKIN);
#else
	    iomux_set(RMII_CLKOUT);
#endif 
	    iomux_set(RMII_RXERR);
	    iomux_set(RMII_CRS);
	    iomux_set(RMII_MD);
	    iomux_set(RMII_MDCLK);

	    if(INVALID_GPIO != PHY_PWR_EN_GPIO)
       { 
            //reset power
	        gpio_direction_output(PHY_PWR_EN_GPIO, !PHY_PWR_EN_VALUE);
	        msleep(20);
	        gpio_direction_output(PHY_PWR_EN_GPIO, PHY_PWR_EN_VALUE);
       }	
    }else {
        if(INVALID_GPIO != PHY_PWR_EN_GPIO)
        {
	         gpio_direction_output(PHY_PWR_EN_GPIO, !PHY_PWR_EN_VALUE);
        } 
    }

    return 0;
}

#define BIT_EMAC_SPEED      (1 << 1)
static int rk29_vmac_speed_switch(int speed)
{
    //printk("%s--speed=%d\n", __FUNCTION__, speed);
    if (10 == speed) {
        writel_relaxed(readl_relaxed(RK30_GRF_BASE + GRF_SOC_CON1) & (~BIT_EMAC_SPEED) | (BIT_EMAC_SPEED << 16), RK30_GRF_BASE + GRF_SOC_CON1);
    } else {
        writel_relaxed(readl_relaxed(RK30_GRF_BASE + GRF_SOC_CON1) | ( BIT_EMAC_SPEED) | (BIT_EMAC_SPEED << 16), RK30_GRF_BASE + GRF_SOC_CON1);
    }
}

static int rk30_rmii_extclk_sel(void)
{
#ifdef RMII_EXT_CLK
    rmii_extclk_sel = 1; //0:select internal divider clock, 1:select external input clock
#endif 
    return rmii_extclk_sel; 
}

struct rk29_vmac_platform_data board_vmac_data = {
	.vmac_register_set = rk30_vmac_register_set,
	.rmii_io_init = rk30_rmii_io_init,
	.rmii_io_deinit = rk30_rmii_io_deinit,
	.rmii_power_control = rk30_rmii_power_control,
	.rmii_speed_switch = rk29_vmac_speed_switch,
    .rmii_extclk_sel = rk30_rmii_extclk_sel,
};
